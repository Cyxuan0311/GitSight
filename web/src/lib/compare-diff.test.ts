import { describe, expect, it } from 'vitest';

import {
  activityInWindow,
  compareWindows,
  diffByKey,
  withinWindow,
} from '@/lib/compare-diff';

import type { Issue, PullRequest, Report } from '@/lib/types';

const NOW = Date.parse('2026-03-10T12:00:00Z');

function pr(overrides: Partial<PullRequest> = {}): PullRequest {
  return {
    number: 1,
    title: 'A pull request',
    state: 'open',
    created_at: '2026-03-09T10:00:00Z',
    merged_at: '',
    author: 'alice',
    labels: '',
    html_url: 'https://example.test/p/1',
    ...overrides,
  };
}

function issue(overrides: Partial<Issue> = {}): Issue {
  return {
    number: 1,
    title: 'An issue',
    state: 'open',
    created_at: '2026-03-09T10:00:00Z',
    closed_at: '',
    author: 'alice',
    labels: '',
    html_url: 'https://example.test/i/1',
    ...overrides,
  };
}

function report(overrides: Partial<Report> = {}): Report {
  return {
    owner: 'acme',
    repo: 'gadget',
    period: 'last 7 days',
    pull_requests: { total: 0, open: 0, closed: 0, merged: 0, avg_merge_hours: 0, items: [] },
    commits: { total: 0, authors: 0, top_author: '', activity: [], weekday: [], items: [] },
    issues: { total: 0, open: 0, closed: 0, avg_close_hours: 0, items: [] },
    releases: { total: 0, latest: '', items: [] },
    contributors: { total: 0, top: '', top_list: '' },
    summary: '',
    ...overrides,
  } as Report;
}

describe('diffing two lists by identity', () => {
  it('separates the items each list has alone', () => {
    const diff = diffByKey([{ id: 'a' }, { id: 'b' }], [{ id: 'b' }, { id: 'c' }], (item) => item.id);

    expect(diff.onlyFirst.map((i) => i.id)).toEqual(['a']);
    expect(diff.onlySecond.map((i) => i.id)).toEqual(['c']);
    expect(diff.shared.map((i) => i.id)).toEqual(['b']);
  });
});

describe('the window a timestamp belongs to', () => {
  it('counts by day, the way the daemon filters', () => {
    // The window starts three days before "now".
    expect(withinWindow('2026-03-07T00:00:01Z', 3, NOW)).toBe(true);
    expect(withinWindow('2026-03-07T00:00:00Z', 3, NOW)).toBe(true);
    expect(withinWindow('2026-03-06T23:59:59Z', 3, NOW)).toBe(false);
  });

  it('treats an empty timestamp as outside every window', () => {
    expect(withinWindow('', 7, NOW)).toBe(false);
  });
});

describe('what changed inside a window', () => {
  it('separates the pull requests opened from the ones merged', () => {
    const opened = pr({ number: 1, created_at: '2026-03-09T10:00:00Z' });
    const merged = pr({
      number: 2,
      created_at: '2026-01-01T10:00:00Z',
      merged_at: '2026-03-09T12:00:00Z',
      state: 'merged',
    });
    const old = pr({ number: 3, created_at: '2026-01-01T10:00:00Z' });

    const activity = activityInWindow(
      report({
        pull_requests: { total: 3, open: 2, closed: 0, merged: 1, avg_merge_hours: 0, items: [opened, merged, old] },
      }),
      7,
      NOW,
    );

    expect(activity.createdPrs.map((p) => p.number)).toEqual([1]);
    expect(activity.mergedPrs.map((p) => p.number)).toEqual([2]);
  });

  it('separates the issues opened from the ones closed', () => {
    const opened = issue({ number: 1 });
    const closed = issue({
      number: 2,
      state: 'closed',
      created_at: '2026-01-01T10:00:00Z',
      closed_at: '2026-03-09T12:00:00Z',
    });

    const activity = activityInWindow(
      report({
        issues: { total: 2, open: 1, closed: 1, avg_close_hours: 0, items: [opened, closed] },
      }),
      7,
      NOW,
    );

    expect(activity.createdIssues.map((i) => i.number)).toEqual([1]);
    expect(activity.closedIssues.map((i) => i.number)).toEqual([2]);
  });
});

describe('comparing two windows', () => {
  it('finds the items only the longer window has', () => {
    const short = report({
      pull_requests: { total: 1, open: 1, closed: 0, merged: 0, avg_merge_hours: 0, items: [pr({ number: 1 })] },
    });
    const long = report({
      pull_requests: {
        total: 2,
        open: 2,
        closed: 0,
        merged: 0,
        avg_merge_hours: 0,
        items: [pr({ number: 1 }), pr({ number: 2 })],
      },
    });

    const diff = compareWindows(short, long, 7, NOW);

    expect(diff.onlyShort.prs).toHaveLength(0);
    expect(diff.onlyLong.prs.map((p) => p.number)).toEqual([2]);
    expect(diff.truncated).toBe(false);
  });

  it('says so when the longer window lost an item to the item limit', () => {
    // The longer window should hold everything the shorter one does, so a
    // missing item means its list was cut short.
    const short = report({
      pull_requests: { total: 2, open: 2, closed: 0, merged: 0, avg_merge_hours: 0, items: [pr({ number: 1 }), pr({ number: 2 })] },
    });
    const long = report({
      pull_requests: { total: 1, open: 1, closed: 0, merged: 0, avg_merge_hours: 0, items: [pr({ number: 1 })] },
    });

    const diff = compareWindows(short, long, 7, NOW);

    expect(diff.onlyShort.prs.map((p) => p.number)).toEqual([2]);
    expect(diff.truncated).toBe(true);
  });
});
