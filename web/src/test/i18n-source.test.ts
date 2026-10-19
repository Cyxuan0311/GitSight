import { readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';

import en from '@/i18n/locales/en.json';
import zh from '@/i18n/locales/zh.json';

function flatten(o: Record<string, unknown>, prefix = ''): Map<string, string> {
  const out = new Map<string, string>();
  for (const [key, value] of Object.entries(o)) {
    if (value && typeof value === 'object') {
      for (const [k, v] of flatten(value as Record<string, unknown>, `${prefix}${key}.`)) out.set(k, v);
      continue;
    }
    out.set(`${prefix}${key}`, String(value));
  }
  return out;
}

/**
 * Everything here is a property of the checked-in files: the tables themselves
 * and the source that reads them. None of it needs a mounted page, so none of it
 * pays for one.
 *
 * The scan below reads source and renders nothing, so it also lives apart from
 * the suite that walks every page: sharing a worker would hold a slot while a
 * purely static check runs.
 */
function sourceFiles(dir: string, acc: string[] = []): string[] {
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (entry === 'node_modules' || entry === 'dist' || entry === 'test') continue;
    if (statSync(full).isDirectory()) sourceFiles(full, acc);
    else if (/\.tsx?$/.test(entry) && !/\.test\.tsx?$/.test(entry)) acc.push(full);
  }
  return acc;
}

describe('source stays free of hardcoded English', () => {
  const here = dirname(fileURLToPath(import.meta.url));
  const files = sourceFiles(join(here, '..'));

  // Prose the reader sees that must not be baked into the component.
  const offenders: string[] = [];

  for (const file of files) {
    const source = readFileSync(file, 'utf8');
    const relative = file.slice(here.length + 1);

    // A JSX text node: >Some Words Here<
    for (const m of source.matchAll(/>\s*([A-Z][A-Za-z]+(?:[ ][a-z]+)+)\s*</g)) {
      const text = m[1]!.trim();
      // A handful are genuinely language-neutral: a format name or a unit.
      if (['Report'].includes(text)) continue;
      offenders.push(`${relative}: JSX text "${text}"`);
    }

    // An aria-label or title given as a literal rather than through t().
    for (const m of source.matchAll(/(?:aria-label|title)="([^"]*[A-Za-z]{3}[^"]*)"/g)) {
      const value = m[1]!.trim();
      // Placeholders and format hints are values, not prose.
      if (/^(main|red|ghp_\.\.\.|owner\/repo|curl\/curl|auto)$/.test(value)) continue;
      if (/^https?:/.test(value)) continue;
      if (m[0]!.includes('http')) continue;
      offenders.push(`${relative}: literal ${m[0]}`);
    }
  }

  it('finds none', () => {
    expect(offenders).toEqual([]);
  });
});

describe('every t() lookup resolves', () => {
  const here = dirname(fileURLToPath(import.meta.url));
  const files = sourceFiles(join(here, '..'));
  const enKeys = new Set(flatten(en).keys());
  const zhKeys = new Set(flatten(zh).keys());

  // A key that is missing renders as the raw key name in both locales, which is
  // how English leaks into a Chinese console through an attribute nobody reads.
  const missing: string[] = [];

  for (const file of files) {
    const source = readFileSync(file, 'utf8');
    const relative = file.slice(here.length + 1);
    for (const m of source.matchAll(/\bt\(\s*'([a-z0-9_]+(?:\.[a-zA-Z0-9_]+)+)'/g)) {
      const key = m[1]!;
      if (!enKeys.has(key)) missing.push(`${relative}: en is missing ${key}`);
      if (!zhKeys.has(key)) missing.push(`${relative}: zh is missing ${key}`);
    }
  }

  it('finds none', () => {
    expect(missing).toEqual([]);
  });

  it('covers the keys the shared components depend on', () => {
    for (const key of ['app.toggleTheme', 'app.loading', 'app.retry', 'ui.resizePanel']) {
      expect(enKeys.has(key), key).toBe(true);
      expect(zhKeys.has(key), key).toBe(true);
    }
  });
});

describe('translation tables', () => {
  const enFlat = flatten(en);
  const zhFlat = flatten(zh);

  it('define the same keys in both locales', () => {
    expect([...zhFlat.keys()].sort()).toEqual([...enFlat.keys()].sort());
  });

  it('has no empty string', () => {
    for (const [key, value] of [...enFlat, ...zhFlat]) {
      expect(value.trim(), `${key} is blank`).not.toBe('');
    }
  });

  it('actually translates, apart from names that are the same either way', () => {
    const identicalOnPurpose = new Set([
      'app.name',
      'report.title',
      'settings.md',
      'settings.pdf',
      'settings.html',
      'ui.branchPlaceholder',
    ]);
    const same = [...enFlat]
      .filter(([key, value]) => zhFlat.get(key) === value && !identicalOnPurpose.has(key))
      .map(([key]) => key);
    expect(same).toEqual([]);
  });
});
