import { cleanup, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { formatPeriod, weekdayLabels, weekdayName } from '@/lib/format';
import { setLanguage } from '@/i18n';

import i18n from '@/i18n';

import { renderWithProviders } from '@/test/setup';
import { ReportOverview, WeekdayChart } from '@/components/report/ReportParts';
import type { Report } from '@/lib/types';

const CJK = /[一-鿿]/;

afterEach(async () => {
  cleanup();
  await setLanguage('en');
});

/**
 * A report exactly as the daemon would have produced it *before* the console
 * learned to relabel it: period and weekday names rendered in the analysis's own
 * language, which is the case that used to leak Chinese into an English console.
 */
const report = (overrides: Partial<Report> = {}): Report =>
  ({
    owner: 'acme',
    repo: 'gadget',
    period: '最近 7 天',
    period_days: 7,
    repository: undefined,
    pull_requests: { total: 2, open: 1, closed: 1, merged: 1, avg_merge_hours: 12, merged_count: 1, items: [] },
    commits: {
      total: 2,
      authors: 1,
      top_author: 'alice (1 commits)',
      activity: [
        { label: '10-16', commits: 2 },
        { label: '10-15', commits: 0 },
      ],
      weekday: [
        { day: 'Sun', index: 0, commits: 0 },
        { day: 'Mon', index: 1, commits: 3 },
        { day: 'Tue', index: 2, commits: 1 },
        { day: 'Wed', index: 3, commits: 0 },
        { day: 'Thu', index: 4, commits: 0 },
        { day: 'Fri', index: 5, commits: 0 },
        { day: 'Sat', index: 6, commits: 0 },
      ],
      items: [],
    },
    issues: { total: 0, open: 0, closed: 0, avg_close_hours: 0, closed_count: 0, items: [] },
    releases: { total: 0, latest: '', items: [] },
    contributors: { total: 2, top: 'alice (9 contributions)', items: [] },
    languages: [],
    top_labels: '',
    summary: '',
    warnings: [],
    ...overrides,
  }) as Report;

describe('formatPeriod', () => {
  it('relabels a rolling window in the console language', async () => {
    await setLanguage('en');
    expect(formatPeriod('最近 7 天', 7, i18n.t)).toBe('last 7 days');

    await setLanguage('zh');
    expect(formatPeriod('last 7 days', 7, i18n.t)).toBe('最近 7 天');
  });

  it('keeps the stored string for an explicit date range', async () => {
    // A range is dates, which need no translation, and there is no day count.
    expect(formatPeriod('2026-01-01..2026-01-08', 0, i18n.t)).toBe('2026-01-01..2026-01-08');
    expect(formatPeriod('2026-01-01..2026-01-08', undefined, i18n.t)).toBe('2026-01-01..2026-01-08');
  });
});

describe('weekdayLabels', () => {
  it('follows the console language', async () => {
    await setLanguage('en');
    const english = weekdayLabels();
    expect(english).toHaveLength(7);
    expect(english).toContain('Mon');
    expect(english.some((d) => CJK.test(d))).toBe(false);

    await setLanguage('zh');
    const chinese = weekdayLabels();
    expect(chinese).not.toEqual(english);
    expect(chinese.some((d) => CJK.test(d))).toBe(true);
  });

  it('prefers the index over the daemon-rendered name', () => {
    const labels = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
    // The daemon says "Mon"; the console is in Chinese and must not show it.
    expect(weekdayName({ day: 'Mon', index: 1, commits: 3 }, ['日', '一', '二', '三', '四', '五', '六'])).toBe('一');
    // An older payload without the index still works.
    expect(weekdayName({ day: 'Mon', commits: 3 }, labels)).toBe('Mon');
  });
});

describe('report views relabel in the console language', () => {
  it('shows the busiest weekday in English, not the analysis language', async () => {
    await setLanguage('en');
    renderWithProviders(<ReportOverview report={report()} />);
    expect(screen.getByText('Mon')).toBeInTheDocument();
    expect(document.body.textContent).not.toMatch(CJK);
  });

  it('labels the whole weekday chart in the console language', async () => {
    await setLanguage('zh');
    renderWithProviders(<WeekdayChart report={report()} />);
    const text = document.body.textContent ?? '';
    // The daemon's English abbreviations must be gone.
    expect(text).not.toContain('Mon');
    expect(text).not.toContain('Sun');
  });
});

