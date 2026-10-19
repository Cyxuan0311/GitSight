import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';

import { parseCsvSections } from '@/lib/csv';
import { parseMarkdown, safeHref, type MdBlock } from '@/lib/markdown';

/**
 * Runs the parsers over artifacts a real daemon produced (captured from
 * `gs serve` with GS_FIXTURE_DIR), so the export preview is checked against the
 * exporter's actual output rather than a hand-written approximation of it.
 *
 * The captured report deliberately contains a pull-request title carrying
 * markup, which must survive as text.
 */
const DIR = join(dirname(fileURLToPath(import.meta.url)), 'fixtures', 'exports');

const read = (name: string) => readFileSync(join(DIR, `report.${name}`), 'utf8');

const md = read('md');
const csv = read('csv');

function textOf(spans: { text: string }[]): string {
  return spans.map((span) => span.text).join('');
}

describe('real markdown export', () => {
  const blocks = parseMarkdown(md);

  it('parses into the expected kinds', () => {
    expect(blocks.length).toBeGreaterThan(8);
    expect(blocks[0]).toMatchObject({ kind: 'heading', level: 1 });
    expect(blocks.filter((b) => b.kind === 'table').length).toBeGreaterThan(2);
    expect(blocks.some((b) => b.kind === 'code')).toBe(true);
    expect(blocks.some((b) => b.kind === 'hr')).toBe(true);
  });

  it('never leaves a parse error behind', () => {
    // Every block must be fully typed; a malformed one would show as a stray "#"
    // or "|" in the preview.
    for (const block of blocks as MdBlock[]) {
      if (block.kind === 'paragraph' || block.kind === 'heading')
        expect(textOf(block.spans).length).toBeGreaterThan(0);
    }
  });

  it('reads every table, including the metric grids', () => {
    for (const block of blocks) {
      if (block.kind !== 'table') continue;
      expect(block.header.length).toBeGreaterThan(0);
      for (const row of block.rows) expect(row.length).toBe(block.header.length);
    }
  });

  it('keeps links usable and drops the "#" the exporter writes for a missing URL', () => {
    const hrefs = blocks.flatMap((block) => {
      if (block.kind === 'table') return block.rows.flat().flatMap((cell) => cell.map((s) => s.href));
      if (block.kind === 'paragraph' || block.kind === 'heading')
        return block.spans.map((s) => s.href);
      return [];
    });
    expect(hrefs.some((href) => href?.startsWith('https://example.test/'))).toBe(true);
    expect(hrefs.every((href) => href === undefined || safeHref(href) === href)).toBe(true);
  });

  it('keeps a repository title as text rather than markup', () => {
    const withScript = blocks.some((block) =>
      block.kind === 'table'
        ? textOf(block.rows.flat().flat()).includes('<script>alert(1)</script>')
        : false,
    );
    expect(withScript).toBe(true);
  });
});

describe('real csv export', () => {
  const sections = parseCsvSections(csv);

  it('finds every section the exporter wrote', () => {
    expect(sections.map((s) => s.title)).toEqual([
      'metrics',
      'pull_requests',
      'commits',
      'issues',
      'releases',
      'languages',
    ]);
  });

  it('reads the metric rows with their quoted commas intact', () => {
    const metrics = sections[0]!;
    expect(metrics.header).toEqual(['key', 'value']);
    const list = metrics.rows.find((row) => row[0] === 'top_contributor_list');
    expect(list?.[1]).toBe('alice (9), bob (3)');
    const summary = metrics.rows.find((row) => row[0] === 'summary');
    expect(summary?.[1]).toContain('The repository acme/gadget');
  });

  it('gives every row the header\'s column count', () => {
    for (const section of sections) {
      for (const row of section.rows) expect(row.length).toBe(section.header.length);
    }
  });
});
