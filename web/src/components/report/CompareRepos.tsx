import { useTranslation } from 'react-i18next';

import { CompareMetricsTable } from '@/components/report/CompareParts';

import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { EmptyState, Table, Td } from '@/components/ui/feedback';
import { periodOf } from '@/lib/compare-diff';
import { formatBytes, splitTop, totalLanguages } from '@/lib/format';
import type { Report } from '@/lib/types';

/**
 * Two repositories, side by side.
 *
 * A batch already holds a full report per repository, so comparing two of
 * them needs no second run: the same window, the same metrics, two answers.
 */
export function CompareRepos({ left, right }: { left: Report; right: Report }) {
  const { t } = useTranslation();

  return (
    <div className="space-y-4">
      <Card>
        <CardHeader>
          <CardTitle className="text-base">{t('batch.compareRepos')}</CardTitle>
        </CardHeader>
        <CardContent className="px-0 pb-0">
          <CompareMetricsTable
            a={periodOf(left)}
            b={periodOf(right)}
            labelA={left.repo}
            labelB={right.repo}
          />
        </CardContent>
      </Card>

      <div className="grid gap-3 lg:grid-cols-2">
        <RepoFacts report={left} />
        <RepoFacts report={right} />
      </div>
    </div>
  );
}

function RepoFacts({ report }: { report: Report }) {
  const { t } = useTranslation();
  const languages = report.languages ?? [];
  const total = totalLanguages(report);

  return (
    <Card>
      <CardHeader>
        <CardTitle className="text-sm">
          {report.owner}/{report.repo}
        </CardTitle>
      </CardHeader>
      <CardContent className="space-y-3">
        <Table>
          <tbody>
            <tr>
              <Td className="text-xs text-muted">{t('compare.topAuthor')}</Td>
              <Td className="text-right text-xs text-ink">
                {splitTop(report.commits.top_author).name || '-'}
              </Td>
            </tr>
            <tr>
              <Td className="text-xs text-muted">{t('compare.topContributor')}</Td>
              <Td className="text-right text-xs text-ink">
                {splitTop(report.contributors.top).name || '-'}
              </Td>
            </tr>
            <tr>
              <Td className="text-xs text-muted">{t('report.latest')}</Td>
              <Td className="text-right text-xs text-ink">{report.releases.latest || '-'}</Td>
            </tr>
            <tr>
              <Td className="text-xs text-muted">{t('report.languages')}</Td>
              <Td className="text-right text-xs text-ink">
                {languages.length === 0 ? (
                  '-'
                ) : (
                  <span className="nums">
                    {languages
                      .slice(0, 3)
                      .map((slice) => `${slice.name} ${slice.percent.toFixed(0)}%`)
                      .join(' · ')}
                    {total > 0 ? (
                      <span className="ml-1 text-muted">({formatBytes(total)})</span>
                    ) : null}
                  </span>
                )}
              </Td>
            </tr>
          </tbody>
        </Table>
      </CardContent>
    </Card>
  );
}

/** Exported for the empty state the batch page shows before two are picked. */
export function NoReposChosen() {
  const { t } = useTranslation();
  return <EmptyState className="py-8" title={t('batch.pickTwoRepos')} />;
}
