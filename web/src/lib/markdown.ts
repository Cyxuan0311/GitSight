/**
 * A parser for the Markdown the `md` exporter emits, used by the export
 * preview.
 *
 * It returns a node tree rather than an HTML string so the preview can render
 * it with React and never touches `dangerouslySetInnerHTML`. That matters here
 * because a report embeds repository titles, commit messages and descriptions
 * verbatim -- attacker-controlled text that must not become markup.
 *
 * The supported grammar is deliberately the subset the exporter produces:
 * ATX headings, horizontal rules, GFM tables, fenced code blocks, paragraphs,
 * and the inline forms `**strong**`, `*emphasis*`, `` `code` `` and
 * `[text](url)`.
 */

import { splitDelimitedRow } from './row';

export interface MdSpan {
  text: string;
  strong?: boolean;
  emphasis?: boolean;
  code?: boolean;
  href?: string;
}

export type MdAlign = 'left' | 'center' | 'right' | null;

export type MdBlock =
  | { kind: 'heading'; level: number; spans: MdSpan[] }
  | { kind: 'paragraph'; spans: MdSpan[] }
  | { kind: 'code'; text: string }
  | { kind: 'hr' }
  | { kind: 'table'; header: MdSpan[][]; align: MdAlign[]; rows: MdSpan[][][] };

const HEADING = /^(#{1,6})\s+(.*)$/;
const RULE = /^\s{0,3}(?:-{3,}|\*{3,}|_{3,})\s*$/;
const FENCE = /^\s*```/;
const DELIMITER_CELL = /^\s*:?-{1,}:?\s*$/;

/**
 * Keeps only link targets a preview should be able to follow. Anything without
 * an explicit scheme is treated as inert: the exporter writes "#" when a pull
 * request carries no URL, and a bare relative path would otherwise be resolved
 * against the console's own origin.
 */
export function safeHref(raw: string): string | undefined {
  const url = raw.trim();
  const scheme = /^([a-z][a-z0-9+.-]*):/i.exec(url)?.[1]?.toLowerCase();
  if (scheme === 'http' || scheme === 'https' || scheme === 'mailto') return url;
  return undefined;
}

/**
 * Splits one table row into cells, tolerating a leading and trailing pipe.
 * The CSV reader splits on the same rules with a different delimiter.
 */
export function splitTableRow(line: string): string[] {
  return splitDelimitedRow(line, '|');
}

function isDelimiterRow(line: string): boolean {
  const cells = splitTableRow(line);
  return cells.length > 0 && cells.every((cell) => DELIMITER_CELL.test(cell));
}

function alignOf(cell: string): MdAlign {
  const left = cell.startsWith(':');
  const right = cell.endsWith(':');
  if (left && right) return 'center';
  if (right) return 'right';
  if (left) return 'left';
  return null;
}

function parseLink(src: string, start: number): { spans: MdSpan[]; end: number } | null {
  let i = start + 1;
  while (i < src.length) {
    if (src[i] === '\\') {
      i += 2;
      continue;
    }
    if (src[i] === ']') break;
    i++;
  }
  if (i >= src.length || src[i + 1] !== '(') return null;
  // Balance the parentheses: a target may legitimately contain them, and
  // stopping at the first ")" would leave the remainder as stray text.
  let depth = 1;
  let close = i + 2;
  while (close < src.length && depth > 0) {
    if (src[close] === '(') depth++;
    else if (src[close] === ')') depth--;
    if (depth > 0) close++;
  }
  if (depth !== 0) return null;

  const label = src.slice(start + 1, i);
  const href = safeHref(src.slice(i + 2, close));
  const inner = parseInline(label);
  const spans = href
    ? inner.map((span) => ({ ...span, href }))
    : inner;
  return { spans: spans.length > 0 ? spans : [{ text: label, href }], end: close + 1 };
}

export function parseInline(src: string): MdSpan[] {
  const spans: MdSpan[] = [];
  let plain = '';
  const flush = () => {
    if (plain) spans.push({ text: plain });
    plain = '';
  };

  let i = 0;
  while (i < src.length) {
    const ch = src[i]!;

    if (ch === '\\' && i + 1 < src.length) {
      plain += src[i + 1];
      i += 2;
      continue;
    }

    if (ch === '`') {
      const end = src.indexOf('`', i + 1);
      if (end > i + 1) {
        flush();
        spans.push({ text: src.slice(i + 1, end), code: true });
        i = end + 1;
        continue;
      }
    }

    if (ch === '[') {
      const link = parseLink(src, i);
      if (link) {
        flush();
        spans.push(...link.spans);
        i = link.end;
        continue;
      }
    }

    if (ch === '*') {
      const marker = src.startsWith('**', i) ? '**' : '*';
      const end = src.indexOf(marker, i + marker.length);
      if (end > i + marker.length) {
        flush();
        const mark = marker === '**' ? 'strong' : 'emphasis';
        const inner = parseInline(src.slice(i + marker.length, end)).map((span) => ({
          ...span,
          [mark]: true,
        }));
        spans.push(...inner);
        i = end + marker.length;
        continue;
      }
    }

    plain += ch;
    i++;
  }

  flush();
  return spans;
}

function startsBlock(line: string): boolean {
  return (
    line.trim() === '' ||
    FENCE.test(line) ||
    RULE.test(line) ||
    HEADING.test(line) ||
    line.trim().startsWith('|')
  );
}

/** Splits into lines without inventing a trailing empty one after a final newline. */
export function splitLines(src: string): string[] {
  const lines = src.replace(/\r\n?/g, '\n').split('\n');
  if (lines.length > 1 && lines[lines.length - 1] === '') lines.pop();
  return lines;
}

export function parseMarkdown(src: string): MdBlock[] {
  const lines = splitLines(src);
  const blocks: MdBlock[] = [];
  let i = 0;

  while (i < lines.length) {
    const line = lines[i]!;

    if (!line.trim()) {
      i++;
      continue;
    }

    if (FENCE.test(line)) {
      const body: string[] = [];
      i++;
      while (i < lines.length && !FENCE.test(lines[i]!)) {
        body.push(lines[i]!);
        i++;
      }
      i++; // closing fence, or past the end for an unterminated block
      blocks.push({ kind: 'code', text: body.join('\n') });
      continue;
    }

    if (RULE.test(line)) {
      blocks.push({ kind: 'hr' });
      i++;
      continue;
    }

    const heading = HEADING.exec(line);
    if (heading) {
      blocks.push({
        kind: 'heading',
        level: heading[1]!.length,
        spans: parseInline(heading[2]!.trim()),
      });
      i++;
      continue;
    }

    if (line.trim().startsWith('|') && i + 1 < lines.length && isDelimiterRow(lines[i + 1]!)) {
      const header = splitTableRow(line).map(parseInline);
      const align = splitTableRow(lines[i + 1]!).map(alignOf);
      i += 2;
      const rows: MdSpan[][][] = [];
      while (i < lines.length && lines[i]!.trim().startsWith('|') && lines[i]!.trim() !== '') {
        rows.push(splitTableRow(lines[i]!).map(parseInline));
        i++;
      }
      blocks.push({ kind: 'table', header, align, rows });
      continue;
    }

    const paragraph: string[] = [];
    while (i < lines.length && !startsBlock(lines[i]!)) {
      paragraph.push(lines[i]!.trim());
      i++;
    }
    if (paragraph.length === 0) i++; // a lone block marker; skip it
    else blocks.push({ kind: 'paragraph', spans: parseInline(paragraph.join(' ')) });
  }

  return blocks;
}

/** Flattens blocks to plain text, for the raw/markdown side-by-side toggle. */
export function markdownToText(src: string): string {
  return parseMarkdown(src)
    .map((block) => {
      switch (block.kind) {
        case 'heading':
          return block.spans.map((span) => span.text).join('');
        case 'paragraph':
          return block.spans.map((span) => span.text).join('');
        case 'code':
          return block.text;
        case 'hr':
          return '---';
        case 'table':
          return block.rows
            .map((row) => row.map((cell) => cell.map((span) => span.text).join('')).join(' | '))
            .join('\n');
      }
    })
    .join('\n\n');
}