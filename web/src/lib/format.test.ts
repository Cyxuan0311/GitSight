import { describe, expect, it } from 'vitest';

import {
  busiestWeekday,
  closedOnly,
  computeDelta,
  firstLine,
  formatCompact,
  formatDuration,
  isExplicitRange,
  languageSlices,
  parseRange,
  periodDays,
  repositoryUrl,
  splitLabels,
  splitTop,
  splitTopList,
} from '@/lib/format';
import type { Report } from '@/lib/types';

/**
 * These assertions encode rules that come from the C implementation rather
 * than from taste. If the analyzer's counting changes, these must change with
 * it -- that is the point of the file.
 */

function report(overrides: Partial<Report> = {}): Report {
  return {
    owner: 'acme',
    repo: 'gadget',
    period: 'last 7 days',
    pull_requests: { total: 0, open: 0, closed: 0, merged: 0, items: [] },
    commits: { total: 0, authors: 0, top_author: '', activity: [], weekday: [], items: [] },
    issues: { total: 0, open: 0, closed: 0, items: [] },
    releases: { total: 0, latest: '', items: [] },
    contributors: { total: 0, top: '', top_list: '' },
    summary: '',
    ...overrides,
  };
}

describe('closedOnly', () => {
  it('excludes merged, because the analyzer counts them inside closed', () => {
    const r = report({
      pull_requests: { total: 5, open: 2, closed: 3, merged: 1, items: [] },
    });
    // Reporting 3 next to 1 merged would double-count the 3 that closed.
    expect(closedOnly(r)).toBe(2);
  });

  it('never goes negative on inconsistent input', () => {
    const r = report({
      pull_requests: { total: 1, open: 0, closed: 0, merged: 2, items: [] },
    });
    expect(closedOnly(r)).toBe(0);
  });
});

describe('splitTop', () => {
  it('parses the "name (N commits)" form', () => {
    expect(splitTop('alice (42 commits)')).toEqual({ name: 'alice', count: 42 });
  });

  it('parses the "name (N contributions)" form', () => {
    expect(splitTop('bob (7 contributions)')).toEqual({ name: 'bob', count: 7 });
  });

  it('returns a null count for a bare name', () => {
    expect(splitTop('alice')).toEqual({ name: 'alice', count: null });
  });

  it('keeps a name that itself contains parentheses intact', () => {
    expect(splitTop('user (bot) (12 commits)')).toEqual({ name: 'user (bot)', count: 12 });
  });

  it('handles the empty string', () => {
    expect(splitTop('')).toEqual({ name: '', count: null });
  });
});

describe('splitTopList', () => {
  it('parses the comma-joined contributor list', () => {
    expect(splitTopList('alice (9), bob (3), carol (1)')).toEqual([
      { name: 'alice', count: 9 },
      { name: 'bob', count: 3 },
      { name: 'carol', count: 1 },
    ]);
  });

  it('returns nothing for an empty list', () => {
    expect(splitTopList('')).toEqual([]);
  });

  it('drops entries that carry no count', () => {
    expect(splitTopList('alice, bob (3)')).toEqual([{ name: 'bob', count: 3 }]);
  });
});

describe('splitLabels', () => {
  it('splits the joined string and trims each label', () => {
    expect(splitLabels('bug, ui, docs')).toEqual(['bug', 'ui', 'docs']);
  });

  it('tolerates a missing value', () => {
    expect(splitLabels(undefined)).toEqual([]);
    expect(splitLabels('')).toEqual([]);
  });

  it('drops empty segments from a trailing separator', () => {
    expect(splitLabels('bug, ')).toEqual(['bug']);
  });
});

describe('period', () => {
  it('detects the explicit range form', () => {
    expect(isExplicitRange('2026-01-01..2026-01-31')).toBe(true);
    expect(isExplicitRange('last 30 days')).toBe(false);
  });

  it('splits an explicit range', () => {
    expect(parseRange('2026-01-01..2026-01-31')).toEqual({
      start: '2026-01-01',
      end: '2026-01-31',
    });
  });

  it('returns null for a rolling window', () => {
    expect(parseRange('last 7 days')).toBeNull();
  });

  it('reads the day count out of a rolling window', () => {
    expect(periodDays('last 30 days')).toBe(30);
    expect(periodDays('last 7 days')).toBe(7);
  });
});

describe('computeDelta', () => {
  it('reports a rise as a percentage', () => {
    const delta = computeDelta(10, 15);
    expect(delta.direction).toBe('up');
    expect(delta.label).toBe('+50%');
  });

  it('reports a fall as a percentage', () => {
    const delta = computeDelta(10, 5);
    expect(delta.direction).toBe('down');
    expect(delta.label).toBe('-50%');
  });

  it('shows the absolute change when the base is zero', () => {
    // A percentage would be infinite here; the CLI shows the raw count.
    const delta = computeDelta(0, 7);
    expect(delta.direction).toBe('unknown');
    expect(delta.label).toBe('+7');
  });

  it('shows a dash when both sides are zero', () => {
    expect(computeDelta(0, 0)).toMatchObject({ direction: 'flat', label: '-' });
  });

  it('rounds to whole percent', () => {
    expect(computeDelta(3, 4).label).toBe('+33%');
  });
});

describe('formatters', () => {
  it('formats sub-hour durations in minutes', () => {
    expect(formatDuration(0.5)).toBe('30m');
  });

  it('formats hour-scale durations', () => {
    expect(formatDuration(6.25)).toBe('6.3h');
  });

  it('converts long durations to days', () => {
    expect(formatDuration(72)).toBe('3.0d');
  });

  it('shows a dash for a missing or zero duration', () => {
    expect(formatDuration(undefined)).toBe('-');
    expect(formatDuration(0)).toBe('-');
  });

  it('compacts only large numbers', () => {
    expect(formatCompact(999)).toBe('999');
    expect(formatCompact(25_000)).toMatch(/25/);
  });

  it('takes the first line of a commit message', () => {
    expect(firstLine('fix the thing\n\nlong body here')).toBe('fix the thing');
  });

  it('handles an empty message', () => {
    expect(firstLine('')).toBe('');
  });
});

describe('busiestWeekday', () => {
  it('picks the highest weekday count', () => {
    const r = report({
      commits: {
        total: 5,
        authors: 2,
        top_author: '',
        activity: [],
        weekday: [
          { day: 'Mon', commits: 1 },
          { day: 'Tue', commits: 4 },
          { day: 'Wed', commits: 2 },
        ],
        items: [],
      },
    });
    expect(busiestWeekday(r)).toEqual({ day: 'Tue', commits: 4 });
  });

  it('returns null when every day is empty', () => {
    const r = report({
      commits: {
        total: 0,
        authors: 0,
        top_author: '',
        activity: [],
        weekday: [
          { day: 'Mon', commits: 0 },
          { day: 'Tue', commits: 0 },
        ],
        items: [],
      },
    });
    expect(busiestWeekday(r)).toBeNull();
  });
});

describe('repositoryUrl', () => {
  it('builds platform-correct links', () => {
    expect(repositoryUrl('github', 'curl', 'curl')).toBe('https://github.com/curl/curl');
    expect(repositoryUrl('gitlab', 'group', 'proj')).toBe('https://gitlab.com/group/proj');
    expect(repositoryUrl('codeberg', 'acme', 'gadget')).toBe(
      'https://codeberg.org/acme/gadget',
    );
  });

  it('strips a .git suffix', () => {
    expect(repositoryUrl('github', 'curl', 'curl.git')).toBe('https://github.com/curl/curl');
  });

  it('returns null without an owner and repo', () => {
    expect(repositoryUrl('github', '', '')).toBeNull();
  });
});

describe('languageSlices', () => {
  it('assigns a stable colour per index', () => {
    const r = report({
      languages: [
        { name: 'C', bytes: 61000, percent: 61 },
        { name: 'Go', bytes: 39000, percent: 39 },
      ],
    });
    const slices = languageSlices(r);
    expect(slices).toHaveLength(2);
    expect(slices[0]!.color).not.toBe(slices[1]!.color);
  });

  it('tolerates a missing language block', () => {
    expect(languageSlices(report())).toEqual([]);
  });
});