import type { Issue, PullRequest, Release, Report } from '@/lib/types';

/**
 * Comparing two windows item by item.
 *
 * The daemon fetches each window separately and filters it on the item's
 * `updated_at`, so the shorter window's list is a subset of the longer one's
 * -- unless the longer list was cut off by the item limit. That makes two
 * different questions worth asking, and the aggregate table answers neither:
 *
 *   - what changed inside the recent window (a pull request opened, merged,
 *     an issue closed), and
 *   - which items each window has that the other does not.
 */

/** Two lists of the same kind of item, compared by identity. */
export interface ItemDiff<T> {
  /** In the first list only. */
  onlyFirst: T[];
  /** In the second list only. */
  onlySecond: T[];
  /** In both. */
  shared: T[];
}

export function diffByKey<T>(first: T[], second: T[], key: (item: T) => string): ItemDiff<T> {
  const secondKeys = new Set(second.map(key));
  const firstKeys = new Set(first.map(key));
  return {
    onlyFirst: first.filter((item) => !secondKeys.has(key(item))),
    onlySecond: second.filter((item) => !firstKeys.has(key(item))),
    shared: first.filter((item) => secondKeys.has(key(item))),
  };
}

const prKey = (pr: PullRequest) => String(pr.number);
const issueKey = (issue: Issue) => String(issue.number);
const releaseKey = (release: Release) => release.tag_name;

/**
 * The day an item belongs to.
 *
 * The daemon filters on the date, not the instant, so the comparison has to
 * use the same granularity or a window would disagree with itself by a few
 * hours at the boundary.
 */
function dayOf(iso: string): string {
  return iso.slice(0, 10);
}

/** The first day of a window that ends today. */
function windowStart(days: number, now: number): string {
  return new Date(now - days * 86_400_000).toISOString().slice(0, 10);
}

/** Whether an item's timestamp falls inside a window that ends today. */
export function withinWindow(iso: string, days: number, now = Date.now()): boolean {
  if (!iso) return false;
  return dayOf(iso) >= windowStart(days, now);
}

/** What changed inside a window, split by the kind of change. */
export interface WindowActivity {
  /** Pull requests opened in the window. */
  createdPrs: PullRequest[];
  /** Pull requests merged in the window, whether or not they were opened there. */
  mergedPrs: PullRequest[];
  /** Issues opened in the window. */
  createdIssues: Issue[];
  /** Issues closed in the window. */
  closedIssues: Issue[];
  /** Releases published in the window. */
  releases: Release[];
}

export function activityInWindow(report: Report, days: number, now = Date.now()): WindowActivity {
  return {
    createdPrs: report.pull_requests.items.filter((pr) => withinWindow(pr.created_at, days, now)),
    mergedPrs: report.pull_requests.items.filter((pr) => withinWindow(pr.merged_at, days, now)),
    createdIssues: report.issues.items.filter((issue) => withinWindow(issue.created_at, days, now)),
    closedIssues: report.issues.items.filter((issue) => withinWindow(issue.closed_at, days, now)),
    releases: report.releases.items.filter((release) =>
      withinWindow(release.published_at, days, now),
    ),
  };
}

/** The items one window has that the other does not. */
export interface WindowDifference {
  prs: PullRequest[];
  issues: Issue[];
  releases: Release[];
}

export interface CompareWindows {
  /** What changed inside the shorter window. */
  activity: WindowActivity;
  /** Items the shorter window has that the longer one does not. */
  onlyShort: WindowDifference;
  /** Items the longer window has that the shorter one does not. */
  onlyLong: WindowDifference;
  /**
   * True when the longer window is missing an item the shorter one has.
   *
   * The longer window covers more ground, so it can only be missing something
   * because its list was cut short by the item limit -- which makes the
   * comparison incomplete and worth saying out loud.
   */
  truncated: boolean;
}

export function compareWindows(
  short: Report,
  long: Report,
  shortDays: number,
  now = Date.now(),
): CompareWindows {
  const prs = diffByKey(short.pull_requests.items, long.pull_requests.items, prKey);
  const issues = diffByKey(short.issues.items, long.issues.items, issueKey);
  const releases = diffByKey(short.releases.items, long.releases.items, releaseKey);

  return {
    activity: activityInWindow(short, shortDays, now),
    onlyShort: { prs: prs.onlyFirst, issues: issues.onlyFirst, releases: releases.onlyFirst },
    onlyLong: { prs: prs.onlySecond, issues: issues.onlySecond, releases: releases.onlySecond },
    truncated: prs.onlyFirst.length > 0 || issues.onlyFirst.length > 0 || releases.onlyFirst.length > 0,
  };
}
