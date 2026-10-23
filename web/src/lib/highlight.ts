/**
 * Minimal, dependency-free tokenizers for the code the console displays.
 *
 * The fields involved are narrow: the Raw file editor shows JSON, and the
 * snippets shown to the reader are shell scripts. Both are structured enough
 * that a small scanner colors them more faithfully than a generic highlighter
 * would, and plain text passes through untouched.
 *
 * The output is a flat token list whose texts concatenate back to the input
 * exactly, so rendered text never differs from the source -- only coloring
 * spans wrap it.
 */

/** The languages a display surface may hold. */
export type Lang = 'text' | 'json' | 'shell';

export type TokenKind =
  | 'text'
  | 'string'
  | /** A JSON object key. */ 'key'
  | 'number'
  | /** JSON true/false/null. */ 'literal'
  | /** Structure characters: braces, brackets, `,` `:` `;` `|`. */ 'punct'
  | 'comment'
  | /** Shell keywords: if, then, esac... */ 'keyword'
  | /** Shell $variables. */ 'variable'
  | /** Shell command flags: -v, --all. */ 'flag';

export interface Token {
  text: string;
  kind: TokenKind;
}

/** Splits text into colored tokens; plain text and unknown languages pass through. */
export function tokenize(text: string, lang: Lang): Token[] {
  if (!text) return [];
  if (lang === 'json') return scanJson(text);
  if (lang === 'shell') return scanShell(text);
  return [{ text, kind: 'text' }];
}

/* -------------------------------------------------------------------- json */

// Sticky, so every match starts exactly at the current cursor.
const JSON_STRING = /"(?:[^"\\\n]|\\.)*"?/y;
const JSON_NUMBER = /-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?/y;
const JSON_WORD = /[A-Za-z0-9_+\-.]+/y;

/**
 * Colors pretty-printed JSON. The scanner tolerates an unfinished document,
 * because the editor retokenizes on every keystroke and a half-written
 * string must still color -- its closing quote simply is not there yet.
 */
function scanJson(text: string): Token[] {
  const out: Token[] = [];
  let plain = '';
  const push = (token: Token) => {
    if (plain) {
      out.push({ text: plain, kind: 'text' });
      plain = '';
    }
    out.push(token);
  };

  let i = 0;
  while (i < text.length) {
    const c = text[i]!;

    // Whitespace and punctuation first: they never start a structured token.
    if (c <= ' ') {
      /* Control and space characters, including the newline. */
      plain += c;
      i++;
      continue;
    }
    if (JSON_PUNCT.has(c)) {
      push({ text: c, kind: 'punct' });
      i++;
      continue;
    }

    if (c === '"') {
      JSON_STRING.lastIndex = i;
      const raw = JSON_STRING.exec(text)?.[0] ?? text.slice(i);
      // A string followed by `:` colors as the key it is; skip whitespace back.
      let colon = i + raw.length;
      while (text[colon] === ' ' || text[colon] === '\t') colon++;
      push({ text: raw, kind: text[colon] === ':' ? 'key' : 'string' });
      i += raw.length;
      continue;
    }

    if (c === '-' || (c >= '0' && c <= '9')) {
      JSON_NUMBER.lastIndex = i;
      const raw = JSON_NUMBER.exec(text)?.[0];
      if (raw) {
        push({ text: raw, kind: 'number' });
        i += raw.length;
        continue;
      }
    }

    JSON_WORD.lastIndex = i;
    const raw = JSON_WORD.exec(text)?.[0] ?? c;
    const kind =
      raw === 'true' || raw === 'false' || raw === 'null' ? 'literal' : 'text';
    push({ text: raw, kind });
    i += raw.length;
  }

  if (plain) out.push({ text: plain, kind: 'text' });
  return out;
}

const JSON_PUNCT = new Set(['{', '}', '[', ']', ',', ':']);

/* ------------------------------------------------------------------- shell */

/** The keywords of the POSIX/Bash control grammar, the few that matter. */
const SHELL_KEYWORDS = new Set([
  'if', 'then', 'elif', 'else', 'fi', 'for', 'while', 'until', 'do', 'done',
  'case', 'esac', 'in', 'function', 'return', 'local', 'export', 'set',
  'unset', 'read', 'shift', 'trap', 'cd', 'exit', 'source', 'alias',
  'echo', 'printf', 'test', 'true', 'false',
]);

const SHELL_WORD = /[A-Za-z0-9_/.@+]+/y;
const SHELL_NUMBER = /^\d+(?:\.\d+)?$/;
const SHELL_VAR = /\$\{[^}\n]*\}?|\$[A-Za-z_]\w*|\$[@*#?$!0-9]/y;
const SHELL_FLAG = /--?[A-Za-z][\w-]*/y;
const SHELL_OPS = new Set([';', '|', '&', '(', ')', '<', '>', '=', '!']);

/** A flag or comment may only start at a word boundary. */
function atBoundary(prev: string): boolean {
  return prev === '' || prev <= ' ' || SHELL_OPS.has(prev) || prev === ';';
}

function scanShell(text: string): Token[] {
  const out: Token[] = [];
  let plain = '';
  const push = (token: Token) => {
    if (plain) {
      out.push({ text: plain, kind: 'text' });
      plain = '';
    }
    out.push(token);
  };

  let prev = '';
  let i = 0;
  while (i < text.length) {
    const c = text[i]!;

    if (c <= ' ') {
      plain += c;
      prev = c;
      i++;
      continue;
    }

    // Comments run to end of line; `#` mid-word (as in `a#b`) is not one.
    if (c === '#' && atBoundary(prev)) {
      const end = text.indexOf('\n', i);
      const raw = end === -1 ? text.slice(i) : text.slice(i, end);
      push({ text: raw, kind: 'comment' });
      i += raw.length;
      prev = '#';
      continue;
    }

    // Single-quoted strings have no escapes; double-quoted honor \\.
    if (c === "'" || c === '"') {
      const end = i + 1;
      let j = end;
      while (j < text.length && text[j] !== c) j += text[j] === '\\' ? 2 : 1;
      const raw = j < text.length ? text.slice(i, j + 1) : text.slice(i);
      push({ text: raw, kind: 'string' });
      i += raw.length;
      prev = ' '; // A flag or comment after a string sits at a word boundary.
      continue;
    }

    if (c === '$') {
      SHELL_VAR.lastIndex = i;
      const raw = SHELL_VAR.exec(text)?.[0] ?? c;
      push({ text: raw, kind: 'variable' });
      i += raw.length;
      prev = ' ';
      continue;
    }

    // `--all` and `-v`; a stray `-` (like `--`) falls through as text.
    if (c === '-' && atBoundary(prev)) {
      SHELL_FLAG.lastIndex = i;
      const raw = SHELL_FLAG.exec(text)?.[0];
      if (raw && raw !== '-') {
        push({ text: raw, kind: 'flag' });
        i += raw.length;
        prev = '/';
        continue;
      }
    }

    if (SHELL_OPS.has(c)) {
      push({ text: c, kind: 'punct' });
      i++;
      prev = c;
      continue;
    }

    SHELL_WORD.lastIndex = i;
    const raw = SHELL_WORD.exec(text)?.[0];
    if (raw) {
      const kind = SHELL_KEYWORDS.has(raw)
        ? 'keyword'
        : SHELL_NUMBER.test(raw)
          ? 'number'
          : 'text';
      push({ text: raw, kind });
      i += raw.length;
      prev = '/';
      continue;
    }

    /* Anything unclaimed (backticks, globs, the second dash of `--`)
       stays as literal text rather than being force-colored. */
    plain += c;
    prev = c;
    i++;
  }

  if (plain) out.push({ text: plain, kind: 'text' });
  return out;
}
