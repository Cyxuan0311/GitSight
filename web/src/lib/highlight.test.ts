import { describe, expect, it } from 'vitest';

import { tokenize, type Token } from '@/lib/highlight';

const kinds = (tokens: Token[]): string[] => tokens.map((t) => t.kind);

describe('tokenize', () => {
  it('lets the tokens concatenate back to the input exactly', () => {
    const json = '{"a": 1, "b": [true, false, null], "c": "x"}\n';
    const shell = '#!/bin/sh\nexport PATH="$HOME/bin:$PATH"  # keep\n';
    for (const [lang, text] of [['json', json], ['shell', shell]] as const) {
      const joined = tokenize(text, lang)
        .map((token) => token.text)
        .join('');
      expect(joined).toBe(text);
    }
  });

  it('passes plain text through as one token, whatever it holds', () => {
    expect(kinds(tokenize('anything at all\n学期纪要 | --x', 'text'))).toEqual(['text']);
  });

  it('colors json keys differently from the strings around them', () => {
    const tokens = tokenize('{"repo": "gitsight", "days": 7}', 'json');
    expect(tokens).toEqual([
      { text: '{', kind: 'punct' },
      { text: '"repo"', kind: 'key' },
      { text: ':', kind: 'punct' },
      { text: ' ', kind: 'text' },
      { text: '"gitsight"', kind: 'string' },
      { text: ',', kind: 'punct' },
      { text: ' ', kind: 'text' },
      { text: '"days"', kind: 'key' },
      { text: ':', kind: 'punct' },
      { text: ' ', kind: 'text' },
      { text: '7', kind: 'number' },
      { text: '}', kind: 'punct' },
    ]);
  });

  it('reads json numbers and literals without eating the structure', () => {
    const tokens = tokenize('[1.5e3, -0, true, false, null]', 'json');
    expect(tokens.map((t) => [t.text, t.kind])).toEqual([
      ['[', 'punct'],
      ['1.5e3', 'number'],
      [',', 'punct'],
      [' ', 'text'],
      ['-0', 'number'],
      [',', 'punct'],
      [' ', 'text'],
      ['true', 'literal'],
      [',', 'punct'],
      [' ', 'text'],
      ['false', 'literal'],
      [',', 'punct'],
      [' ', 'text'],
      ['null', 'literal'],
      [']', 'punct'],
    ]);
  });

  it('tolerates a half-written document while the editor types', () => {
    const tokens = tokenize('{"path": " Thi s merges/"', 'json');
    expect(kinds(tokens)).toEqual(['punct', 'key', 'punct', 'text', 'string']);
    // The run of spaces between the key and the string stays in the output.
    const spaced = tokenize('{"path":  "x"}', 'json');
    expect(spaced[2]).toEqual({ text: ':', kind: 'punct' });
    expect(spaced[3]).toEqual({ text: '  ', kind: 'text' });
  });

  it('colors shell comments only where they really start', () => {
    const tokens = tokenize('# note\necho a#b\n', 'shell');
    // `# note` is a comment; `a#b` is a word with a hash in it.
    expect(tokens[0]).toEqual({ text: '# note', kind: 'comment' });
    expect(tokens.filter((t) => t.kind === 'comment')).toHaveLength(1);
    expect(kinds(tokens)).toEqual([
      'comment', 'text', 'keyword', 'text', 'text', 'text', 'text', 'text',
    ]);
  });

  it('colors shell strings, variables, flags and keywords', () => {
    const tokens = tokenize(
      'export PATH="$HOME/bin:$PATH"\ngs --quiet -f "$1"',
      'shell',
    );
    const byKind = (kind: string) =>
      tokens.filter((t) => t.kind === kind).map((t) => t.text);
    // The whole double-quoted string, contents included.
    expect(byKind('string')).toEqual(['"$HOME/bin:$PATH"', '"$1"']);
    expect(byKind('keyword')).toEqual(['export']);
    expect(byKind('flag')).toEqual(['--quiet', '-f']);
    // The brace form belongs to a variable too.
    expect(tokenize('${GS_HOME}\n', 'shell')[0]).toEqual({
      text: '${GS_HOME}',
      kind: 'variable',
    });
  });

  it('keeps a stray dash as text rather than a flag or an operator', () => {
    expect(kinds(tokenize('a -- b\n', 'shell'))).toEqual([
      'text', 'text', 'text', 'text',
    ]);
  });
});
