import { describe, expect, it } from 'vitest';

import {
  checkExportOverrides,
  emptyRunOptions,
  exportOverrideKeys,
  parseExportOverrides,
  parseFilters,
  runOptionsToRequest,
} from '@/components/run/runOptions';

/**
 * The console mirrors the CLI's `--export key=value` syntax, so the parsing
 * rules are the CLI's: one pair per line, a key the exporter knows, and a
 * value that is not empty.
 */
describe('parsing export overrides', () => {
  it('reads one pair per line and ignores blank ones', () => {
    const { pairs, invalid } = parseExportOverrides(
      'show_commits=false\n\n  pdf.margin=40  \nitem_limit=5',
    );
    expect(invalid).toBeUndefined();
    expect(pairs).toEqual(['show_commits=false', 'pdf.margin=40', 'item_limit=5']);
  });

  it('names the first line that is not a pair', () => {
    const { pairs, invalid } = parseExportOverrides('show_commits=false\nbroken');
    expect(invalid).toBe('broken');
    // The pairs read before the bad line are still reported, so the caller
    // can decide whether to submit them.
    expect(pairs).toEqual(['show_commits=false']);
  });

  it('lists the keys in the order they were written', () => {
    expect(exportOverrideKeys('show_commits=false\npdf.margin=40')).toEqual([
      'show_commits',
      'pdf.margin',
    ]);
  });
});

describe('checking export overrides', () => {
  const known = ['show_commits', 'pdf.margin'];

  it('accepts a list of known keys', () => {
    expect(checkExportOverrides('show_commits=false\npdf.margin=40', known)).toBeUndefined();
  });

  it('reports a line that is missing the equals sign', () => {
    expect(checkExportOverrides('show_commits', known)).toEqual({
      kind: 'missing_equals',
      token: 'show_commits',
    });
  });

  it('reports a key the exporter does not know', () => {
    expect(checkExportOverrides('show_pull_request=false', known)).toEqual({
      kind: 'unknown_key',
      token: 'show_pull_request',
    });
  });

  it('skips the key check when the server offered no list', () => {
    expect(checkExportOverrides('anything=false', undefined)).toBeUndefined();
  });
});

describe('filter entries', () => {
  it('splits on newlines and commas and drops blanks', () => {
    expect(parseFilters('author:alice, label:bug\n\nregression')).toEqual([
      'author:alice',
      'label:bug',
      'regression',
    ]);
  });
});

describe('turning the option fields into a request', () => {
  it('leaves everything out when nothing was filled in', () => {
    expect(runOptionsToRequest(emptyRunOptions())).toEqual({});
  });

  it('carries the fields that were filled in', () => {
    const request = runOptionsToRequest({
      ...emptyRunOptions(),
      platform: 'gitlab',
      branch: ' main ',
      lang: 'zh',
      llm_mode: 'trends',
      max_pages: '50',
      api_base: 'https://git.example.com/api/v4',
      proxy: 'http://proxy:8080',
      token: 'glpat-secret',
      filters: 'author:alice, label:bug',
      no_cache: true,
      exportOverrides: 'show_commits=false',
    });

    expect(request).toEqual({
      platform: 'gitlab',
      branch: 'main',
      lang: 'zh',
      llm_mode: 'trends',
      max_pages: 50,
      api_base: 'https://git.example.com/api/v4',
      proxy: 'http://proxy:8080',
      token: 'glpat-secret',
      filters: ['author:alice', 'label:bug'],
      no_cache: true,
      export: ['show_commits=false'],
    });
  });

  it('drops a page count that is not a positive number', () => {
    expect(runOptionsToRequest({ ...emptyRunOptions(), max_pages: '0' }).max_pages).toBeUndefined();
    expect(runOptionsToRequest({ ...emptyRunOptions(), max_pages: 'abc' }).max_pages).toBeUndefined();
    expect(runOptionsToRequest({ ...emptyRunOptions(), max_pages: '12' }).max_pages).toBe(12);
  });

  it('keeps no_cache off when the box is not ticked', () => {
    expect(runOptionsToRequest(emptyRunOptions()).no_cache).toBeUndefined();
  });
});
