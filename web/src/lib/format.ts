import type { TFunction } from 'i18next';

import i18n from '@/i18n';

import type {
  BatchResult,
  CompareResult,
  LanguageSlice,
  PullRequest,
  Report,
  Warning,
  WeekdayBucket,
} from './types';

type Translate = TFunction;

/**
 * Formatting helpers that encode the analysis semantics.
 *
 * Several rules here are not obvious and are inherited from the C code rather
 * than chosen freely. Changing one would make the console disagree with the
 * exported reports, which is the whole point of building the UI on the same
 * engine.
 */

/**
 * `pull_requests.closed` counts merged PRs too, because the analyzer derives
 * open/total/merged and treats the remainder as closed. Reporting `closed`
 * next to `merged` would double-count, so the console shows the remainder.
 */
export function closedOnly(report: Report): number {
  return Math.max(0, report.pull_requests.closed - report.pull_requests.merged);
}

/**
 * The analyzer writes these as "alice (42 commits)" for top_author and
 * "alice (42)" inside top_list, so the trailing unit word is optional. Splits
 * it so the name can be matched without re-parsing at each call site.
 */
export function splitTop(value: string): { name: string; count: number | null } {
  const match = /^(.*?)\s*\((\d+)(?:\s+[^)]*)?\)\s*$/.exec(value.trim());
  if (!match) return { name: value.trim(), count: null };
  return { name: (match[1] ?? '').trim(), count: Number(match[2]) };
}

/** `top_list` arrives as "alice (9), bob (3)". */
export function splitTopList(value: string): { name: string; count: number }[] {
  if (!value.trim()) return [];
  return value
    .split(',')
    .map((part) => splitTop(part))
    .filter((entry): entry is { name: string; count: number } => entry.count !== null);
}

/** Labels are stored as one comma-and-space joined string. */
export function splitLabels(value: string | undefined): string[] {
  if (!value) return [];
  return value
    .split(',')
    .map((label) => label.trim())
    .filter(Boolean);
}

/** `period` is either "last 7 days" or an explicit "2026-01-01..2026-01-31". */
export function isExplicitRange(period: string): boolean {
  return period.includes('..');
}

export function parseRange(period: string): { start: string; end: string } | null {
  if (!isExplicitRange(period)) return null;
  const [start = '', end = ''] = period.split('..');
  return start && end ? { start, end } : null;
}

/** Days covered by a period label, when it is the "last N days" form. */
export function periodDays(period: string): number | null {
  const match = /(\d+)/.exec(period);
  return match?.[1] ? Number(match[1]) : null;
}

/**
 * Formatters are cached per locale.
 *
 * The locale is the console's language, never `undefined`: passing undefined
 * hands the decision to the browser, so a reader who switched the console to
 * English on a Chinese machine kept seeing 1万 rather than 1K. Building an
 * Intl object is not free and these run inside tables and charts, so one per
 * (locale, kind) is kept alive.
 */
const formatterCache = new Map<string, Intl.NumberFormat | Intl.DateTimeFormat>();

function numberFormat(compact: boolean): Intl.NumberFormat {
  const key = `n:${i18n.language}:${compact}`;
  let format = formatterCache.get(key) as Intl.NumberFormat | undefined;
  if (!format) {
    format = new Intl.NumberFormat(i18n.language, {
      notation: compact ? 'compact' : 'standard',
      maximumFractionDigits: 1,
    });
    formatterCache.set(key, format);
  }
  return format;
}

function dateFormat(withTime: boolean): Intl.DateTimeFormat {
  const key = `d:${i18n.language}:${withTime}`;
  let format = formatterCache.get(key) as Intl.DateTimeFormat | undefined;
  if (!format) {
    format = new Intl.DateTimeFormat(i18n.language, {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      ...(withTime ? { hour: '2-digit', minute: '2-digit' } : {}),
    });
    formatterCache.set(key, format);
  }
  return format;
}

/** Turns the exporter's "YYYY-MM-DDTHH:MM:SSZ" into a local date-time. */
export function formatDateTime(iso: string): string {
  if (!iso) return '-';
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return iso;
  return dateFormat(true).format(date);
}

export function formatDate(iso: string): string {
  if (!iso) return '-';
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return iso;
  return dateFormat(false).format(date);
}

/** Relative age for tables where an exact timestamp is too much detail. */
export function formatRelative(epochSeconds: number): string {
  const delta = Math.floor(Date.now() / 1000 - epochSeconds);
  if (!Number.isFinite(delta)) return '-';
  if (delta < 60) return 'just now';
  if (delta < 3600) return `${Math.floor(delta / 60)}m ago`;
  if (delta < 86400) return `${Math.floor(delta / 3600)}h ago`;
  if (delta < 86400 * 30) return `${Math.floor(delta / 86400)}d ago`;
  return formatDate(new Date(epochSeconds * 1000).toISOString());
}

export function formatDuration(hours: number | undefined): string {
  if (hours === undefined || !Number.isFinite(hours) || hours <= 0) return '-';
  if (hours < 1) return `${Math.round(hours * 60)}m`;
  if (hours < 48) return `${hours.toFixed(1)}h`;
  return `${(hours / 24).toFixed(1)}d`;
}

export function formatNumber(value: number): string {
  return numberFormat(false).format(value);
}

/**
 * Compact for large counts, full digits below ten thousand.
 *
 * The unit suffix comes from the console's language: English gets K/M/B and
 * Chinese gets 万/亿, which is why this has to be told the locale explicitly.
 */
export function formatCompact(value: number): string {
  return numberFormat(value >= 10_000).format(value);
}

/** Commit messages arrive with newlines collapsed to spaces by the drivers. */
export function firstLine(message: string): string {
  const [line = ''] = message.split('\n');
  return line.trim();
}

/**
 * Weekday abbreviations in the console's language.
 *
 * The daemon also sends a `day` string, but it is rendered with the *report's*
 * language, so a reader who switched the console would otherwise see a chart
 * labelled in the previous language. The payload carries the index for this.
 */
export function weekdayLabels(): string[] {
  const out: string[] = [];
  for (let i = 0; i < 7; i++) {
    const day = new Date(Date.UTC(2024, 0, 7 + i)); // 2024-01-07 was a Sunday
    out.push(
      new Intl.DateTimeFormat(i18n.language, { weekday: 'short', timeZone: 'UTC' }).format(day),
    );
  }
  return out;
}

/** The label one weekday entry should be shown under, in the console's language. */
export function weekdayName(entry: WeekdayBucket, labels: string[]): string {
  if (typeof entry.index === 'number' && labels[entry.index]) return labels[entry.index]!;
  return entry.day;
}

/** Busiest weekday, derived from the weekday series. */
export function busiestWeekday(
  report: Report,
  labels?: string[],
): { day: string; commits: number } | null {
  const best = report.commits.weekday.reduce<{ day: string; commits: number } | null>(
    (acc, entry) => (entry.commits > (acc?.commits ?? -1) ? entry : acc),
    null,
  );
  if (!best || best.commits <= 0) return null;
  return { day: labels ? weekdayName(best, labels) : best.day, commits: best.commits };
}

/**
 * The report window, labelled in the console's language.
 *
 * The daemon renders `period` in the language the *analysis* ran in, and that is
 * frozen with the job: a report produced under zh keeps saying "last 7 days" in
 * Chinese even after the reader switches the console to English. When the
 * payload carries `period_days` the window is a rolling one and can be relabelled
 * here; an explicit --from/--to range is dates, which need no translation, so the
 * stored string is used as-is.
 */
export function formatPeriod(
  period: string,
  periodDays: number | undefined,
  t: Translate,
): string {
  if (typeof periodDays === 'number' && periodDays > 0) {
    return t('report.periodDays', { days: periodDays });
  }
  return period;
}

/** Peak of the activity series, for scaling bar heights. */
export function activityPeak(report: Report): number {
  return report.commits.activity.reduce((max, entry) => Math.max(max, entry.commits), 0);
}

export function totalLanguages(report: Report): number {
  return (report.languages ?? []).reduce((sum, slice) => sum + slice.bytes, 0);
}

/** Repository URL for an owner/repo pair on the platform that produced it. */
export function repositoryUrl(platform: string, owner: string, repo: string): string | null {
  if (!owner || !repo) return null;
  const clean = (name: string) => name.replace(/\.git$/, '');
  switch (platform) {
    case 'gitlab':
      return `https://gitlab.com/${clean(owner)}/${clean(repo)}`;
    case 'codeberg':
      return `https://codeberg.org/${clean(owner)}/${clean(repo)}`;
    case 'gitea':
      return `https://gitea.com/${clean(owner)}/${clean(repo)}`;
    case 'forgejo':
      return `https://codeberg.org/${clean(owner)}/${clean(repo)}`;
    default:
      return `https://github.com/${clean(owner)}/${clean(repo)}`;
  }
}

/**
 * Percent change between two periods, using the same rules as the markdown
 * comparison table: both zero shows no change, a zero base shows the absolute
 * difference rather than an infinite percentage.
 */
export interface Delta {
  value: number | null;
  direction: 'up' | 'down' | 'flat' | 'unknown';
  label: string;
}

export function computeDelta(a: number, b: number): Delta {
  if (a === 0 && b === 0) return { value: null, direction: 'flat', label: '-' };
  if (a === 0) return { value: null, direction: 'unknown', label: `+${b}` };
  const pct = ((b - a) / a) * 100;
  return {
    value: pct,
    direction: pct > 0 ? 'up' : pct < 0 ? 'down' : 'flat',
    label: `${pct >= 0 ? '+' : ''}${pct.toFixed(0)}%`,
  };
}

/** Metrics compared by the comparison view, in display order. */
export const COMPARE_METRICS = [
  { key: 'pr_total', labelKey: 'compare.metric.prs' },
  { key: 'pr_merged', labelKey: 'compare.metric.merged' },
  { key: 'commit_total', labelKey: 'compare.metric.commits' },
  { key: 'author_total', labelKey: 'compare.metric.authors' },
  { key: 'issue_total', labelKey: 'compare.metric.issues' },
  { key: 'release_total', labelKey: 'compare.metric.releases' },
  { key: 'contributor_total', labelKey: 'compare.metric.contributors' },
] as const;

export type CompareMetricKey = (typeof COMPARE_METRICS)[number]['key'];

export function compareRows(result: CompareResult) {
  const a = result.period_a;
  const b = result.period_b;
  const rows = COMPARE_METRICS.map((metric) => {
    const left = a[metric.key] as number;
    const right = b[metric.key] as number;
    return { ...metric, a: left, b: right, delta: computeDelta(left, right) };
  });
  // Hours only mean something once something was merged or closed.
  const hasMergeTime = a.pr_total > 0 || b.pr_total > 0;
  const hasCloseTime = a.issue_total > 0 || b.issue_total > 0;
  return {
    rows,
    hours: [
      ...(hasMergeTime
        ? [
            {
              key: 'pr_avg_merge_hours' as const,
              labelKey: 'compare.metric.mergeHours',
              a: a.pr_avg_merge_hours,
              b: b.pr_avg_merge_hours,
              delta: computeDelta(a.pr_avg_merge_hours, b.pr_avg_merge_hours),
              format: (v: number) => formatDuration(v) as string | number,
            },
          ]
        : []),
      ...(hasCloseTime
        ? [
            {
              key: 'issue_avg_close_hours' as const,
              labelKey: 'compare.metric.closeHours',
              a: a.issue_avg_close_hours,
              b: b.issue_avg_close_hours,
              delta: computeDelta(a.issue_avg_close_hours, b.issue_avg_close_hours),
              format: (v: number) => formatDuration(v) as string | number,
            },
          ]
        : []),
    ],
  };
}

/** Aggregates the languages series for the chart, padding to a fixed palette. */
export const LANGUAGE_COLORS = [
  'oklch(62% 0.19 250)',
  'oklch(66% 0.17 150)',
  'oklch(70% 0.18 70)',
  'oklch(62% 0.20 300)',
  'oklch(66% 0.18 20)',
  'oklch(70% 0.14 200)',
  'oklch(64% 0.15 130)',
  'oklch(68% 0.16 340)',
];

export function languageSlices(report: Report): (LanguageSlice & { color: string })[] {
  return (report.languages ?? []).map((slice, index) => ({
    ...slice,
    color: LANGUAGE_COLORS[index % LANGUAGE_COLORS.length]!,
  }));
}

export function stateTone(state: string): 'open' | 'closed' | 'merged' | 'neutral' {
  if (state === 'merged') return 'merged';
  if (state === 'open') return 'open';
  if (state === 'closed') return 'closed';
  return 'neutral';
}

/** Pull requests that failed, surfaced next to the report rather than hidden. */
export function collectWarnings(...sources: (Warning[] | undefined)[]): Warning[] {
  return sources.flatMap((list) => list ?? []);
}

export function batchOkCount(result: BatchResult): number {
  return result.reports.filter((entry) => entry.status === 0).length;
}

export function prSearchText(pr: PullRequest): string {
  return `${pr.number} ${pr.title} ${pr.author} ${pr.labels} ${pr.state}`.toLowerCase();
}

/** Byte size of a rendered artifact, for the export preview's size badge. */
export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}
