import { useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';

import { ReportSectionIcon } from './ReportSection';

import { Badge, StateBadge } from '@/components/ui/badge';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/field';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import {
  activityPeak,
  busiestWeekday,
  closedOnly,
  firstLine,
  formatBytes,
  formatDateTime,
  formatDuration,
  formatNumber,
  splitTop,
  splitTopList,
  totalLanguages,
  weekdayLabels,
  weekdayName,
} from '@/lib/format';
import { cn } from '@/lib/cn';
import type { BatchReportEntry, Report } from '@/lib/types';

import { Tooltip } from '@/components/ui/tooltip';

import { LabelChips, StatCard } from '@/components/common';

/* ------------------------------------------------------------- overview --- */

export function ReportOverview({ report }: { report: Report }) {
  const { t } = useTranslation();
  const meta = report.repository;
  const topAuthor = splitTop(report.commits.top_author);
  const busiest = busiestWeekday(report, weekdayLabels());

  return (
    <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
      <StatCard
        label={t('report.pullRequests')}
        icon={<ReportSectionIcon name="pullRequests" />}
        value={formatNumber(report.pull_requests.total)}
        hint={`${report.pull_requests.open} ${t('report.open')} · ${closedOnly(report)} ${t(
          'report.closed',
        )} · ${report.pull_requests.merged} ${t('report.merged')}`}
        tone="accent"
      />
      <StatCard
        label={t('report.commits')}
        icon={<ReportSectionIcon name="commits" />}
        value={formatNumber(report.commits.total)}
        hint={
          topAuthor.count !== null
            ? `${t('report.topAuthor')}: ${topAuthor.name}`
            : `${report.commits.authors} ${t('report.authors')}`
        }
      />
      <StatCard
        label={t('report.issues')}
        icon={<ReportSectionIcon name="issues" />}
        value={formatNumber(report.issues.total)}
        hint={`${report.issues.open} ${t('report.open')} · ${report.issues.closed} ${t('report.closed')}`}
      />
      <StatCard
        label={t('report.releases')}
        icon={<ReportSectionIcon name="releases" />}
        value={formatNumber(report.releases.total)}
        hint={report.releases.latest || t('app.none')}
      />
      <StatCard
        label={t('report.contributors')}
        icon={<ReportSectionIcon name="contributors" />}
        value={formatNumber(report.contributors.total)}
        hint={splitTop(report.contributors.top).name || t('app.none')}
      />
      <StatCard
        label={t('report.avgMerge')}
        icon={<ReportSectionIcon name="merge" />}
        value={formatDuration(report.pull_requests.avg_merge_hours)}
        hint={
          report.pull_requests.merged_count
            ? `${report.pull_requests.merged_count} ${t('report.merged')}`
            : undefined
        }
      />
      <StatCard
        label={t('report.avgClose')}
        icon={<ReportSectionIcon name="close" />}
        value={formatDuration(report.issues.avg_close_hours)}
        hint={
          report.issues.closed_count
            ? `${report.issues.closed_count} ${t('report.closedCount')}`
            : undefined
        }
      />
      <StatCard
        label={t('report.busiestDay')}
        icon={<ReportSectionIcon name="busiest" />}
        value={busiest?.day ?? '-'}
        hint={busiest ? `${formatNumber(busiest.commits)} ${t('report.commitsLabel')}` : undefined}
      />

      {meta ? (
        <>
          <StatCard
            label={t('report.stars')}
            icon={<ReportSectionIcon name="stars" />}
            value={formatNumber(meta.stars)}
          />
          <StatCard
            label={t('report.forks')}
            icon={<ReportSectionIcon name="forks" />}
            value={formatNumber(meta.forks)}
          />
          {meta.watchers > 0 ? (
            <StatCard
              label={t('report.watchers')}
              icon={<ReportSectionIcon name="watchers" />}
              value={formatNumber(meta.watchers)}
            />
          ) : null}
          <StatCard
            label={t('report.openIssues')}
            icon={<ReportSectionIcon name="issues" />}
            value={formatNumber(meta.open_issues)}
          />
          <StatCard
            label={t('report.license')}
            icon={<ReportSectionIcon name="license" />}
            value={meta.license || '-'}
          />
          <StatCard
            label={t('report.language')}
            icon={<ReportSectionIcon name="languages" />}
            value={meta.language || '-'}
          />
          <StatCard
            label={t('report.branch')}
            icon={<ReportSectionIcon name="branch" />}
            value={meta.branch || meta.default_branch || '-'}
          />
          <StatCard
            label={t('report.pushed')}
            icon={<ReportSectionIcon name="pushed" />}
            value={meta.pushed_at ? formatDateTime(meta.pushed_at) : '-'}
          />
          <div className="rounded-card border border-line bg-surface p-4 sm:col-span-2 xl:col-span-2">
            <p className="text-xs text-muted">{t('report.description')}</p>
            <p className="mt-1.5 line-clamp-3 text-sm text-ink-soft">
              {meta.description || '-'}
            </p>
            {meta.created_at ? (
              <p className="mt-2 text-xs text-muted">
                {t('report.created')} {formatDateTime(meta.created_at)}
              </p>
            ) : null}
          </div>
        </>
      ) : null}
    </div>
  );
}

/* -------------------------------------------------------------- charts --- */

/**
 * Daily activity. The exporter labels buckets "MM-DD" newest-first, which reads
 * backwards as a time axis, so the series is reversed for display.
 */
export function ActivityChart({ report }: { report: Report }) {
  const { t } = useTranslation();
  const series = useMemo(() => [...report.commits.activity].reverse(), [report.commits.activity]);
  const peak = activityPeak(report);

  if (series.length === 0 || peak === 0) {
    return <EmptyState className="py-6" title={t('report.noActivity')} />;
  }

  return (
    <div className="flex h-40 items-end gap-[2px]" role="img" aria-label={t('report.activity')}>
      {series.map((bucket) => {
        const height = Math.max(2, Math.round((bucket.commits / peak) * 100));
        return (
          <div
            key={bucket.label}
            className="group relative min-w-0 flex-1"
            title={`${bucket.label}: ${bucket.commits}`}
          >
            <div
              className="rounded-t bg-accent/70 transition-colors group-hover:bg-accent"
              style={{ height: `${height}%` }}
            />
          </div>
        );
      })}
    </div>
  );
}

export function WeekdayChart({ report }: { report: Report }) {
  const { t } = useTranslation();
  const peak = report.commits.weekday.reduce((max, entry) => Math.max(max, entry.commits), 0);
  // Labelled from the weekday index, so the chart follows the console language
  // rather than the one the analysis happened to run in.
  const labels = weekdayLabels();

  if (peak === 0) return <EmptyState className="py-6" title={t('report.noActivity')} />;

  return (
    <div className="space-y-1.5">
      {report.commits.weekday.map((entry) => (
        <div key={entry.day} className="flex items-center gap-2">
          <span className="w-8 shrink-0 text-xs text-muted">{weekdayName(entry, labels)}</span>
          <div className="h-2 flex-1 overflow-hidden rounded-full bg-raised">
            <div
              className="h-full rounded-full bg-accent"
              style={{ width: `${(entry.commits / peak) * 100}%` }}
            />
          </div>
          <span className="nums w-8 shrink-0 text-right text-xs text-muted">{entry.commits}</span>
        </div>
      ))}
    </div>
  );
}

export function LanguageChart({ report }: { report: Report }) {
  const { t } = useTranslation();
  const languages = report.languages ?? [];
  // The percentages alone say how the repository is split; the byte count
  // says how big it is.
  const total = totalLanguages(report);

  if (languages.length === 0) {
    return <EmptyState className="py-6" title={t('app.empty')} />;
  }

  return (
    <div className="space-y-2.5">
      {languages.map((slice) => (
        <div key={slice.name}>
          <div className="flex items-baseline justify-between gap-2 text-xs">
            <span className="truncate font-medium text-ink">{slice.name}</span>
            <span className="nums shrink-0 text-muted">
              {slice.percent.toFixed(1)}%
              {slice.bytes > 0 ? (
                <span className="ml-1.5 text-muted/80">
                  {formatBytes(slice.bytes)}
                </span>
              ) : null}
            </span>
          </div>
          <div className="mt-1 h-1.5 overflow-hidden rounded-full bg-raised">
            <div
              className="h-full rounded-full bg-accent"
              style={{ width: `${Math.min(100, slice.percent)}%` }}
            />
          </div>
        </div>
      ))}
      {total > 0 ? (
        <p className="pt-1 text-xs text-muted">
          {t('report.total')} {formatBytes(total)}
        </p>
      ) : null}
    </div>
  );
}

/* ---------------------------------------------------------------- items --- */

/** Shared row filter box; the tables are small enough not to need pagination. */
function RowFilter({
  value,
  onChange,
}: {
  value: string;
  onChange: (next: string) => void;
}) {
  const { t } = useTranslation();
  return (
    <Input
      value={value}
      onChange={(event) => onChange(event.target.value)}
      placeholder={t('report.filterItems')}
      className="h-8 max-w-xs text-xs"
    />
  );
}

/**
 * Row cap notice. The json and csv exporters always emit every item while the
 * rendered formats stop at `item_limit`, so the console shows everything and
 * says so rather than silently hiding rows behind a default.
 */
function ItemLimitNotice({
  shown,
  total,
  limit,
  expanded,
  onToggle,
}: {
  shown: number;
  total: number;
  limit: number;
  expanded: boolean;
  onToggle: () => void;
}) {
  const { t } = useTranslation();
  if (total <= limit) return null;

  return (
    <p className="flex items-center gap-2 text-xs text-muted">
      <span>{t('report.itemLimit', { shown, total, limit })}</span>
      <button
        type="button"
        onClick={onToggle}
        className="font-medium text-accent hover:underline"
      >
        {expanded ? t('report.showLess') : t('report.showAll')}
      </button>
    </p>
  );
}

export function PullRequestTable({
  report,
  itemLimit,
}: {
  report: Report;
  itemLimit: number;
}) {
  const { t } = useTranslation();
  const [query, setQuery] = useState('');
  const [expanded, setExpanded] = useState(false);

  const needle = query.trim().toLowerCase();
  const matched = useMemo(
    () =>
      needle
        ? report.pull_requests.items.filter((pr) =>
            `${pr.number} ${pr.title} ${pr.author} ${pr.labels} ${pr.state}`
              .toLowerCase()
              .includes(needle),
          )
        : report.pull_requests.items,
    [report.pull_requests.items, needle],
  );

  const shown = expanded ? matched : matched.slice(0, itemLimit);

  return (
    <Card>
      <CardHeader>
        <div className="space-y-1">
          <CardTitle className="flex items-center gap-2">
            <ReportSectionIcon name="pullRequests" />
            {t('report.pullRequests')} · {report.pull_requests.total}
          </CardTitle>
          <ItemLimitNotice
            shown={shown.length}
            total={matched.length}
            limit={itemLimit}
            expanded={expanded}
            onToggle={() => setExpanded((prev) => !prev)}
          />
        </div>
        <RowFilter value={query} onChange={setQuery} />
      </CardHeader>
      <CardContent className="px-0 pb-0">
        {shown.length === 0 ? (
          <EmptyState className="py-8" title={t('report.noItems')} />
        ) : (
          <Table>
            <thead>
              <tr>
                <Th className="w-14">{t('report.colNumber')}</Th>
                <Th>{t('report.colTitle')}</Th>
                <Th className="w-20">{t('report.colState')}</Th>
                <Th className="w-28">{t('report.colAuthor')}</Th>
                <Th className="w-36">{t('report.colCreated')}</Th>
                <Th className="w-36">{t('report.colMerged')}</Th>
              </tr>
            </thead>
            <tbody>
              {shown.map((pr) => (
                <tr key={pr.number} className="hover:bg-raised/60">
                  <Td className="nums">
                    {pr.html_url ? (
                      <a
                        href={pr.html_url}
                        target="_blank"
                        rel="noreferrer"
                        className="text-accent hover:underline"
                      >
                        #{pr.number}
                      </a>
                    ) : (
                      `#${pr.number}`
                    )}
                  </Td>
                  <Td className="text-ink">
                    <span className="line-clamp-2">{pr.title}</span>
                    <LabelChips labels={pr.labels} className="mt-1" limit={5} />
                  </Td>
                  <Td>
                    <StateBadge state={pr.state} />
                  </Td>
                  <Td className="truncate">{pr.author || '-'}</Td>
                  <Td className="nums whitespace-nowrap text-xs">
                    {formatDateTime(pr.created_at)}
                  </Td>
                  <Td className="nums whitespace-nowrap text-xs">
                    {pr.merged_at ? formatDateTime(pr.merged_at) : '-'}
                  </Td>
                </tr>
              ))}
            </tbody>
          </Table>
        )}
      </CardContent>
    </Card>
  );
}

export function CommitTable({ report, itemLimit }: { report: Report; itemLimit: number }) {
  const { t } = useTranslation();
  const [query, setQuery] = useState('');
  const [expanded, setExpanded] = useState(false);

  const needle = query.trim().toLowerCase();
  const matched = useMemo(
    () =>
      needle
        ? report.commits.items.filter((commit) =>
            `${commit.sha} ${commit.message} ${commit.author}`.toLowerCase().includes(needle),
          )
        : report.commits.items,
    [report.commits.items, needle],
  );

  const shown = expanded ? matched : matched.slice(0, itemLimit);

  return (
    <Card>
      <CardHeader>
        <div className="space-y-1">
          <CardTitle className="flex items-center gap-2">
            <ReportSectionIcon name="commits" />
            {t('report.commits')} · {report.commits.total}
          </CardTitle>
          <ItemLimitNotice
            shown={shown.length}
            total={matched.length}
            limit={itemLimit}
            expanded={expanded}
            onToggle={() => setExpanded((prev) => !prev)}
          />
        </div>
        <RowFilter value={query} onChange={setQuery} />
      </CardHeader>
      <CardContent className="px-0 pb-0">
        {shown.length === 0 ? (
          <EmptyState className="py-8" title={t('report.noItems')} />
        ) : (
          <Table>
            <thead>
              <tr>
                <Th className="w-20">{t('report.colDate')}</Th>
                <Th>{t('report.colMessage')}</Th>
                <Th className="w-28">{t('report.colAuthor')}</Th>
              </tr>
            </thead>
            <tbody>
              {shown.map((commit) => (
                <tr key={commit.sha} className="hover:bg-raised/60">
                  <Td className="nums whitespace-nowrap text-xs">
                    {formatDateTime(commit.date)}
                  </Td>
                  <Td className="text-ink">
                    <div className="flex items-baseline gap-2">
                      {commit.html_url ? (
                        <a
                          href={commit.html_url}
                          target="_blank"
                          rel="noreferrer"
                          className="font-mono text-xs text-accent hover:underline"
                        >
                          {commit.sha}
                        </a>
                      ) : (
                        <span className="font-mono text-xs text-muted">{commit.sha}</span>
                      )}
                      <span className="line-clamp-1">{firstLine(commit.message)}</span>
                    </div>
                  </Td>
                  <Td className="truncate">{commit.author || '-'}</Td>
                </tr>
              ))}
            </tbody>
          </Table>
        )}
      </CardContent>
    </Card>
  );
}

export function IssueTable({ report, itemLimit }: { report: Report; itemLimit: number }) {
  const { t } = useTranslation();
  const [query, setQuery] = useState('');
  const [expanded, setExpanded] = useState(false);

  const needle = query.trim().toLowerCase();
  const matched = useMemo(
    () =>
      needle
        ? report.issues.items.filter((issue) =>
            `${issue.number} ${issue.title} ${issue.author} ${issue.labels} ${issue.state}`
              .toLowerCase()
              .includes(needle),
          )
        : report.issues.items,
    [report.issues.items, needle],
  );

  const shown = expanded ? matched : matched.slice(0, itemLimit);

  return (
    <Card>
      <CardHeader>
        <div className="space-y-1">
          <CardTitle className="flex items-center gap-2">
            <ReportSectionIcon name="issues" />
            {t('report.issues')} · {report.issues.total}
          </CardTitle>
          <ItemLimitNotice
            shown={shown.length}
            total={matched.length}
            limit={itemLimit}
            expanded={expanded}
            onToggle={() => setExpanded((prev) => !prev)}
          />
        </div>
        <RowFilter value={query} onChange={setQuery} />
      </CardHeader>
      <CardContent className="px-0 pb-0">
        {shown.length === 0 ? (
          <EmptyState className="py-8" title={t('report.noItems')} />
        ) : (
          <Table>
            <thead>
              <tr>
                <Th className="w-14">{t('report.colNumber')}</Th>
                <Th>{t('report.colTitle')}</Th>
                <Th className="w-20">{t('report.colState')}</Th>
                <Th className="w-28">{t('report.colAuthor')}</Th>
                <Th className="w-36">{t('report.colCreated')}</Th>
                <Th className="w-36">{t('report.colClosed')}</Th>
              </tr>
            </thead>
            <tbody>
              {shown.map((issue) => (
                <tr key={issue.number} className="hover:bg-raised/60">
                  <Td className="nums">
                    {issue.html_url ? (
                      <a
                        href={issue.html_url}
                        target="_blank"
                        rel="noreferrer"
                        className="text-accent hover:underline"
                      >
                        #{issue.number}
                      </a>
                    ) : (
                      `#${issue.number}`
                    )}
                  </Td>
                  <Td className="text-ink">
                    <span className="line-clamp-2">{issue.title}</span>
                    <LabelChips labels={issue.labels} className="mt-1" limit={5} />
                  </Td>
                  <Td>
                    <StateBadge state={issue.state} />
                  </Td>
                  <Td className="truncate">{issue.author || '-'}</Td>
                  <Td className="nums whitespace-nowrap text-xs">
                    {formatDateTime(issue.created_at)}
                  </Td>
                  <Td className="nums whitespace-nowrap text-xs">
                    {issue.closed_at ? formatDateTime(issue.closed_at) : '-'}
                  </Td>
                </tr>
              ))}
            </tbody>
          </Table>
        )}
      </CardContent>
    </Card>
  );
}

export function ReleaseTable({ report, itemLimit }: { report: Report; itemLimit: number }) {
  const { t } = useTranslation();
  const shown = report.releases.items.slice(0, itemLimit);

  if (report.releases.items.length === 0) {
    return (
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <ReportSectionIcon name="releases" />
            {t('report.releases')} · {report.releases.total}
          </CardTitle>
        </CardHeader>
        <CardContent>
          <EmptyState className="py-8" title={t('report.noItems')} />
        </CardContent>
      </Card>
    );
  }

  return (
    <Card>
      <CardHeader>
        <CardTitle className="flex items-center gap-2">
          <ReportSectionIcon name="releases" />
          {t('report.releases')} · {report.releases.total}
        </CardTitle>
      </CardHeader>
      <CardContent className="px-0 pb-0">
        <Table>
          <thead>
            <tr>
              <Th className="w-28">{t('report.colTag')}</Th>
              <Th>{t('report.colName')}</Th>
              <Th className="w-28">{t('report.colAuthor')}</Th>
              <Th className="w-36">{t('report.colPublished')}</Th>
            </tr>
          </thead>
          <tbody>
            {shown.map((release) => (
              <tr key={release.tag_name} className="hover:bg-raised/60">
                <Td className="font-mono text-xs text-ink">
                  {release.html_url ? (
                    <a
                      href={release.html_url}
                      target="_blank"
                      rel="noreferrer"
                      className="text-accent hover:underline"
                    >
                      {release.tag_name}
                    </a>
                  ) : (
                    release.tag_name
                  )}
                </Td>
                <Td className="text-ink">
                  <span className="line-clamp-2">{release.name || '-'}</span>
                  {release.body ? (
                    <details className="mt-1">
                      <summary className="cursor-pointer text-xs text-accent hover:underline">
                        {t('report.releaseNotes')}
                      </summary>
                      <p className="mt-1 max-h-40 overflow-auto text-xs whitespace-pre-wrap text-ink-soft scrollbar-thin">
                        {release.body}
                      </p>
                    </details>
                  ) : null}
                </Td>
                <Td className="truncate">{release.author || '-'}</Td>
                <Td className="nums whitespace-nowrap text-xs">
                  {formatDateTime(release.published_at)}
                </Td>
              </tr>
            ))}
          </tbody>
        </Table>
      </CardContent>
    </Card>
  );
}

export function ContributorList({ report }: { report: Report }) {
  const { t } = useTranslation();
  const top = splitTop(report.contributors.top);
  // The daemon sends the ranked list as well as the single top name; the list
  // is what tells a reader who else carried the work.
  const ranked = splitTopList(report.contributors.top_list);

  if (report.contributors.total === 0) {
    return <EmptyState className="py-6" title={t('report.noItems')} />;
  }

  return (
    <div className="space-y-3">
      <div className="flex items-baseline gap-2">
        <span className="text-2xl font-semibold text-ink">{top.name || '-'}</span>
        <span className="text-xs text-muted">
          {t('report.contributions', { count: top.count ?? report.contributors.total })}
        </span>
      </div>

      {ranked.length > 1 ? (
        <Table>
          <thead>
            <tr>
              <Th className="w-12">{t('report.colRank')}</Th>
              <Th>{t('report.colContributor')}</Th>
              <Th className="w-28 text-right">{t('report.colContributions')}</Th>
            </tr>
          </thead>
          <tbody>
            {ranked.map((entry, index) => (
              <tr key={entry.name} className="hover:bg-raised/60">
                <Td className="nums text-muted">{index + 1}</Td>
                <Td className="truncate text-ink">{entry.name}</Td>
                <Td className="nums text-right">{formatNumber(entry.count)}</Td>
              </tr>
            ))}
          </tbody>
        </Table>
      ) : null}

      <p className="text-xs text-muted">
        {t('report.total')} {formatNumber(report.contributors.total)}
      </p>
    </div>
  );
}

/* -------------------------------------------------------------- summary --- */

/**
 * Minimal markdown renderer for the LLM summary. The exporters carry their own
 * subset renderer for md/txt/html/pdf; this mirrors it closely enough for the
 * headings, bold runs, lists and inline code an LLM actually produces, and
 * refuses raw HTML so a summary cannot inject markup into the console.
 */
export function SummaryPanel({
  summary,
  llmProvider,
  className,
}: {
  summary: string;
  llmProvider?: string;
  className?: string;
}) {
  const { t } = useTranslation();
  if (!summary.trim()) {
    return <EmptyState className="py-6" title={t('report.noSummary')} />;
  }

  const blocks = summary.split(/\n{2,}/);

  return (
    <div className={cn('space-y-3 text-sm leading-relaxed text-ink-soft', className)}>
      {blocks.map((block, index) => {
        const heading = /^\*\*(.+?)\*\*$/.exec(block.trim());
        if (heading) {
          return (
            <h4 key={index} className="text-xs font-semibold tracking-wide text-ink uppercase">
              {heading[1]}
            </h4>
          );
        }
        const bullets = block.split('\n').filter((line) => /^\s*[-*]\s+/.test(line));
        if (bullets.length > 0 && bullets.length === block.split('\n').filter(Boolean).length) {
          return (
            <ul key={index} className="list-disc space-y-1 pl-5">
              {bullets.map((line, i) => (
                <li key={i}>{inline(line.replace(/^\s*[-*]\s+/, ''))}</li>
              ))}
            </ul>
          );
        }
        return <p key={index}>{inline(block)}</p>;
      })}
      {llmProvider ? (
        <p className="text-xs text-muted">{t('report.llmNote', { provider: llmProvider })}</p>
      ) : null}
    </div>
  );
}

function inline(text: string): React.ReactNode {
  // Split on the inline constructs an LLM emits, keeping the delimiters so the
  // output can be assembled as real elements rather than HTML strings.
  const parts = text.split(/(\*\*[^*]+\*\*|`[^`]+`|\*[^*]+\*|\[[^\]]+\]\([^)]+\))/g);
  return parts.map((part, index) => {
    if (part.startsWith('**') && part.endsWith('**')) {
      return (
        <strong key={index} className="font-semibold text-ink">
          {part.slice(2, -2)}
        </strong>
      );
    }
    if (part.startsWith('`') && part.endsWith('`')) {
      return (
        <code key={index} className="rounded bg-raised px-1 py-0.5 font-mono text-xs text-ink">
          {part.slice(1, -1)}
        </code>
      );
    }
    const link = /^\[([^\]]+)\]\(([^)]+)\)$/.exec(part);
    if (link) {
      // Only http(s) links; anything else could smuggle a javascript: URL.
      const href = link[2] ?? '';
      if (!/^https?:\/\//i.test(href)) return <span key={index}>{link[1]}</span>;
      return (
        <a
          key={index}
          href={href}
          target="_blank"
          rel="noreferrer"
          className="text-accent hover:underline"
        >
          {link[1]}
        </a>
      );
    }
    if (part.startsWith('*') && part.endsWith('*') && part.length > 2) {
      return <em key={index}>{part.slice(1, -1)}</em>;
    }
    return <span key={index}>{part}</span>;
  });
}

/* ------------------------------------------------------------- warnings --- */

/**
 * The status of one repository in a batch.
 *
 * A repository can be analysed and still have failed on a single resource, so
 * the partial failure is named rather than hidden behind a green badge: the
 * report is complete except for what the warning says it is missing.
 */
export function BatchStatus({ entry }: { entry: BatchReportEntry }) {
  const { t } = useTranslation();
  const warnings = entry.warnings ?? [];
  return (
    <div className="flex flex-col items-start gap-1">
      <Badge tone={entry.status === 0 ? 'ok' : 'danger'}>
        {entry.status === 0 ? t('batch.ok') : t('batch.failed')}
      </Badge>
      {warnings.length > 0 ? (
        <Tooltip
          content={
            <ul className="space-y-1">
              {warnings.map((warning) => (
                <li key={`${warning.resource}-${warning.error}`}>
                  <span className="font-medium">{warning.resource}</span>
                  {': '}
                  {warning.detail || warning.error}
                </li>
              ))}
            </ul>
          }
        >
          <Badge tone="warn">{t('report.warnings')}</Badge>
        </Tooltip>
      ) : null}
    </div>
  );
}

export function WarningList({ warnings }: { warnings: NonNullable<Report['warnings']> }) {
  const { t } = useTranslation();
  if (warnings.length === 0) {
    return <p className="text-xs text-muted">{t('report.noWarnings')}</p>;
  }
  return (
    <ul className="space-y-1.5">
      {warnings.map((warning) => (
        <li key={`${warning.resource}-${warning.error}`} className="flex items-start gap-2">
          <Badge tone="warn">{warning.resource}</Badge>
          <span className="min-w-0 flex-1 text-xs text-muted">
            {warning.detail || warning.error}
          </span>
        </li>
      ))}
    </ul>
  );
}