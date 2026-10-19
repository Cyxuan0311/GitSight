import { describe, expect, it } from 'vitest';

import { clampCell, parseCsvSections } from './csv';

const CSV = [
  '# metrics',
  'key,value',
  'repository,acme',
  'name,gadget',
  'top_contributor_list,"alice (9), bob (3)"',
  '',
  '# pull_requests',
  'number,title,author',
  '42,Fix the widget,alice',
  '41,"Add docs, and more",bob',
  '',
  '# languages',
  'name,bytes,percent',
  'C,61000,61.0',
].join('\n');

describe('parseCsvSections', () => {
  it('splits on the exporter\'s section comments', () => {
    const sections = parseCsvSections(CSV);
    expect(sections.map((s) => s.title)).toEqual(['metrics', 'pull_requests', 'languages']);
  });

  it('takes the first row of a section as its header', () => {
    const [metrics] = parseCsvSections(CSV);
    expect(metrics?.header).toEqual(['key', 'value']);
    expect(metrics?.rows).toHaveLength(3);
    expect(metrics?.rows[0]).toEqual(['repository', 'acme']);
  });

  it('keeps a quoted comma inside one cell', () => {
    const [metrics, prs] = parseCsvSections(CSV);
    expect(metrics?.rows[2]).toEqual([
      'top_contributor_list',
      'alice (9), bob (3)',
    ]);
    expect(prs?.rows[1]).toEqual(['41', 'Add docs, and more', 'bob']);
  });

  it('handles a file with no section comments', () => {
    const sections = parseCsvSections('a,b\n1,2\n');
    expect(sections).toHaveLength(1);
    expect(sections[0]?.title).toBe('');
    expect(sections[0]?.header).toEqual(['a', 'b']);
    expect(sections[0]?.rows).toEqual([['1', '2']]);
  });

  it('keeps a row whose cells are empty', () => {
    const sections = parseCsvSections('# s\na,b,c\n1,,\n');
    expect(sections[0]?.rows).toEqual([['1', '', '']]);
  });

  it('returns nothing for an empty file', () => {
    expect(parseCsvSections('')).toEqual([]);
    expect(parseCsvSections('# only a heading\n')).toEqual([]);
  });
});

describe('clampCell', () => {
  it('leaves a short value alone', () => {
    expect(clampCell('  alice  ')).toBe('alice');
  });

  it('collapses whitespace and marks a cut', () => {
    expect(clampCell('a\n\n  b')).toBe('a b');
    const long = clampCell('x'.repeat(200));
    expect(long).toHaveLength(160);
    expect(long.endsWith('…')).toBe(true);
  });

  it('honours a custom cap', () => {
    expect(clampCell('abcdef', 4)).toBe('abc…');
  });
});