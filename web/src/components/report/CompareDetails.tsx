import { useMemo } from 'react';
import { useTranslation } from 'react-i18next';

import { ReportSectionIcon } from '@/components/report/ReportSection';

import { Badge } from '@/components/ui/badge';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { EmptyState } from '@/components/ui/feedback';
import { compareWindows, type WindowDifference } from '@/lib/compare-diff';
import { formatDateTime } from '@/lib/format';
import type { CompareResult, Issue, PullRequest, Release } from '@/lib/types';

/**
 * The item-level view of a comparison.
 *
 * The metric table above says how much happened; this says what happened.
 * Each group links back to the platform, so a reader can go from a number to
 * the pull request that produced it.
 */
export function CompareDetails({ result }: { result: CompareResult }) {
  const { t } = useTranslation();

  const diff = useMemo(
    () => compareWindows(result.report_a, result.report_b, result.period_days_a),
    [result],
  );

  const groups = [
    {
      key: 'createdPrs',
      label: t('compare.createdPrs'),
      items: diff.activity.createdPrs,
      kind: 'pr' as const,
    },
    {
      key: 'mergedPrs',
      label: t('compare.mergedPrs'),
      items: diff.activity.mergedPrs,
      kind: 'pr' as const,
    },
    {
      key: 'createdIssues',
      label: t('compare.createdIssues'),
      items: diff.activity.createdIssues,
      kind: 'issue' as const,
    },
    {
      key: 'closedIssues',
      label: t('compare.closedIssues'),
      items: diff.activity.closedIssues,
      kind: 'issue' as const,
    },
    {
      key: 'releases',
      label: t('compare.publishedReleases'),
      items: diff.activity.releases,
      kind: 'release' as const,
    },
  ].filter((group) => group.items.length > 0);

  return (
    <Card>
      <CardHeader>
        <CardTitle className="flex items-center gap-2">
          <ReportSectionIcon name="diff" />
          {t('compare.details')}
        </CardTitle>
        <span className="text-xs text-muted">
          {t('compare.windowDays', { count: result.period_days_a })}
        </span>
      </CardHeader>
      <CardContent className="space-y-4">
        {diff.truncated ? (
          <p className="rounded-field border border-warn/40 bg-warn/10 px-3 py-2 text-xs text-warn">
            {t('compare.truncated')}
          </p>
        ) : null}

        {groups.length === 0 ? (
          <EmptyState className="py-6" title={t('compare.noDetails')} />
        ) : (
          <div className="grid gap-3 lg:grid-cols-2">
            {groups.map((group) => (
              <ItemGroup
                key={group.key}
                label={group.label}
                count={group.items.length}
                items={group.items}
                kind={group.kind}
              />
            ))}
          </div>
        )}

        <DifferenceSummary
          onlyShort={diff.onlyShort}
          onlyLong={diff.onlyLong}
          shortDays={result.period_days_a}
          longDays={result.period_days_b}
        />
      </CardContent>
    </Card>
  );
}

type ItemKind = 'pr' | 'issue' | 'release';

function ItemGroup({
  label,
  count,
  items,
  kind,
}: {
  label: string;
  count: number;
  items: (PullRequest | Issue | Release)[];
  kind: ItemKind;
}) {
  const { t } = useTranslation();
  return (
    <div className="rounded-card border border-line">
      <div className="flex items-center justify-between gap-2 border-b border-line px-3 py-2">
        <span className="text-xs font-medium text-ink">{label}</span>
        <Badge tone="neutral" className="nums font-normal">
          {count}
        </Badge>
      </div>
      <ul className="max-h-48 divide-y divide-line overflow-auto scrollbar-thin">
        {items.map((item) => (
          <li key={itemKey(item, kind)} className="px-3 py-1.5 text-xs">
            <ItemRow item={item} kind={kind} />
          </li>
        ))}
      </ul>
      {count === 0 ? (
        <p className="px-3 py-2 text-xs text-muted">{t('compare.noItems')}</p>
      ) : null}
    </div>
  );
}

function ItemRow({ item, kind }: { item: PullRequest | Issue | Release; kind: ItemKind }) {
  const { t } = useTranslation();
  const url = 'html_url' in item ? item.html_url : '';

  if (kind === 'release') {
    const release = item as Release;
    return (
      <span className="flex items-baseline gap-2">
        <span className="font-mono text-[11px] text-ink">{release.tag_name}</span>
        <span className="min-w-0 flex-1 truncate text-muted">{release.name}</span>
        <span className="nums shrink-0 text-[11px] text-muted">
          {formatDateTime(release.published_at)}
        </span>
      </span>
    );
  }

  const { number, title, state } = item as PullRequest;
  return (
    <span className="flex items-baseline gap-2">
      {url ? (
        <a
          href={url}
          target="_blank"
          rel="noreferrer"
          className="shrink-0 font-mono text-[11px] text-accent hover:underline"
        >
          #{number}
        </a>
      ) : (
        <span className="shrink-0 font-mono text-[11px] text-muted">#{number}</span>
      )}
      <span className="min-w-0 flex-1 truncate text-ink">{title}</span>
      <span className="shrink-0 text-[11px] text-muted">{t(`state.${state}`)}</span>
    </span>
  );
}

function itemKey(item: PullRequest | Issue | Release, kind: ItemKind): string {
  if (kind === 'release') return (item as Release).tag_name;
  return String((item as PullRequest).number);
}

/**
 * The two lists a set difference produces.
 *
 * An item in the shorter window that the longer one lacks means the longer
 * list was cut short, so the reader is told rather than left to infer it from
 * a count.
 */
function DifferenceSummary({
  onlyShort,
  onlyLong,
  shortDays,
  longDays,
}: {
  onlyShort: WindowDifference;
  onlyLong: WindowDifference;
  shortDays: number;
  longDays: number;
}) {
  const { t } = useTranslation();
  const shortCount = onlyShort.prs.length + onlyShort.issues.length + onlyShort.releases.length;
  const longCount = onlyLong.prs.length + onlyLong.issues.length + onlyLong.releases.length;

  return (
    <details className="rounded-card border border-line">
      <summary className="cursor-pointer px-3 py-2 text-xs font-medium text-ink">
        {t('compare.difference')}
        <span className="ml-2 font-normal text-muted">
          {t('compare.differenceCounts', { short: shortCount, long: longCount })}
        </span>
      </summary>
      <div className="grid gap-3 border-t border-line p-3 lg:grid-cols-2">
        <DifferenceList
          label={t('compare.onlyShort', { days: shortDays })}
          prs={onlyShort.prs}
          issues={onlyShort.issues}
          releases={onlyShort.releases}
        />
        <DifferenceList
          label={t('compare.onlyLong', { days: longDays })}
          prs={onlyLong.prs}
          issues={onlyLong.issues}
          releases={onlyLong.releases}
        />
      </div>
    </details>
  );
}

function DifferenceList({
  label,
  prs,
  issues,
  releases,
}: {
  label: string;
  prs: PullRequest[];
  issues: Issue[];
  releases: Release[];
}) {
  const { t } = useTranslation();
  const total = prs.length + issues.length + releases.length;
  return (
    <div>
      <p className="mb-1 text-xs font-medium text-ink">{label}</p>
      {total === 0 ? (
        <p className="text-xs text-muted">{t('compare.noItems')}</p>
      ) : (
        <ul className="space-y-0.5">
          {prs.map((pr) => (
            <li key={`pr-${pr.number}`} className="truncate text-xs text-ink-soft">
              <span className="font-mono text-[11px] text-muted">#{pr.number}</span> {pr.title}
            </li>
          ))}
          {issues.map((issue) => (
            <li key={`issue-${issue.number}`} className="truncate text-xs text-ink-soft">
              <span className="font-mono text-[11px] text-muted">#{issue.number}</span> {issue.title}
            </li>
          ))}
          {releases.map((release) => (
            <li key={`rel-${release.tag_name}`} className="truncate text-xs text-ink-soft">
              <span className="font-mono text-[11px] text-muted">{release.tag_name}</span>{' '}
              {release.name}
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}
