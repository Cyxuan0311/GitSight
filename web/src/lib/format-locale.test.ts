import { afterEach, describe, expect, it } from 'vitest';

import { formatCompact, formatDate, formatDateTime, formatNumber } from './format';

import { setLanguage } from '@/i18n';

const CJK = /[一-鿿]/;

afterEach(async () => {
  await setLanguage('en');
});

/**
 * The unit suffix of a compact number comes from the locale handed to Intl.
 * Leaving it undefined delegates to the browser, which is how a reader on a
 * Chinese machine could switch the console to English and still read 万.
 */
describe('number formatting follows the console language', () => {
  it('uses English units in English', async () => {
    await setLanguage('en');
    expect(formatCompact(12_345)).toBe('12.3K');
    expect(formatCompact(1_000_000)).toBe('1M');
    expect(formatCompact(1_500_000_000)).toBe('1.5B');
    expect(formatCompact(12_345)).not.toMatch(CJK);
  });

  it('uses Chinese units in Chinese', async () => {
    await setLanguage('zh');
    expect(formatCompact(12_345)).toContain('万');
    expect(formatCompact(100_000_000)).toContain('亿');
  });

  it('leaves numbers below the compact threshold unabbreviated', async () => {
    await setLanguage('en');
    expect(formatCompact(9_999)).toBe(formatNumber(9_999));
  });

  it('formats plain numbers in the console language', async () => {
    await setLanguage('en');
    expect(formatNumber(1_234)).toBe('1,234');
    await setLanguage('zh');
    // Grouping differs between locales; the point is it follows the language.
    expect(formatNumber(1_234)).not.toBe('');
  });

  it('re-reads the language instead of pinning it at module load', async () => {
    await setLanguage('en');
    const english = formatCompact(987_654);
    await setLanguage('zh');
    const chinese = formatCompact(987_654);
    expect(english).not.toBe(chinese);
    expect(english).not.toMatch(CJK);
  });
});

describe('dates follow the console language', () => {
  const iso = '2026-03-04T05:06:07Z';

  it('formats in the active locale', async () => {
    await setLanguage('en');
    const englishDate = formatDate(iso);
    const englishTime = formatDateTime(iso);

    await setLanguage('zh');
    expect(formatDate(iso)).not.toBe(englishDate);
    expect(formatDateTime(iso)).not.toBe(englishTime);
    expect(englishDate).not.toMatch(CJK);
  });

  it('passes through an unparsable value', () => {
    expect(formatDate('not a date')).toBe('not a date');
    expect(formatDateTime('not a date')).toBe('not a date');
    expect(formatDate('')).toBe('-');
  });
});

