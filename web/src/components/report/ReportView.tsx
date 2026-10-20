import { ExternalLink } from 'lucide-react';
import { useQuery } from '@tanstack/react-query';
import { useTranslation } from 'react-i18next';

import {
  ActivityChart,
  CommitTable,
  ContributorList,
  IssueTable,
  LanguageChart,
  PullRequestTable,
  ReleaseTable,
  ReportOverview,
  SummaryPanel,
  WarningList,
  WeekdayChart,
} from './ReportParts';
import { ReportPreview } from './ReportPreview';
import { ReportSectionIcon, type ReportIcon } from './ReportSection';

import { Badge } from '@/components/ui/badge';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Tabs, TabsList, TabsPanel, Tab } from '@/components/ui/tabs';
import { CopyButton, LabelChips } from '@/components/common';
import { api } from '@/lib/api';
import { formatPeriod, repositoryUrl } from '@/lib/format';
import type { Report } from '@/lib/types';

/**
 * Renders one repository's analysis.
 *
 * Section visibility comes from the same `export.show_*` flags the exporters
 * honour, so hiding "Issues" here also removes them from a downloaded report.
 */
export function ReportView({
  report,
  jobId,
  platform,
  itemLimit,
  showSections,
  llmProvider,
  lang,
  repoSelector,
}: {
  report: Report;
  jobId: string;
  platform: string;
  itemLimit: number;
  showSections: Record<string, boolean>;
  llmProvider?: string;
  /** Report language; forwarded so the export preview follows it. */
  lang?: string;
  /** Set when the job produced several reports and an export target is needed. */
  repoSelector?: React.ReactNode;
}) {
  const { t } = useTranslation();
  const repoUrl = repositoryUrl(platform, report.owner, report.repo);
  // Drives which export tabs the preview offers, so the switcher always matches
  // what the daemon can actually render.
  const options = useQuery({ queryKey: ['meta-options'], queryFn: api.options });

  const tabs = [
    showSections.show_prs && { key: 'prs', label: t('report.pullRequests'), count: report.pull_requests.total, icon: 'pullRequests' as const },
    showSections.show_commits && { key: 'commits', label: t('report.commits'), count: report.commits.total, icon: 'commits' as const },
    showSections.show_issues && { key: 'issues', label: t('report.issues'), count: report.issues.total, icon: 'issues' as const },
    showSections.show_releases && { key: 'releases', label: t('report.releases'), count: report.releases.total, icon: 'releases' as const },
    showSections.show_contributors && { key: 'contributors', label: t('report.contributors'), count: report.contributors.total, icon: 'contributors' as const },
  ].filter(Boolean) as { key: string; label: string; count: number; icon: ReportIcon }[];

  return (
    <div className="space-y-4">
      <Card>
        <CardHeader>
          <div className="min-w-0 space-y-1">
            <CardTitle className="flex flex-wrap items-center gap-2 text-base">
              <span className="truncate">
                {t('report.title', { owner: report.owner, repo: report.repo })}
              </span>
              <Badge tone="neutral" className="nums font-normal">
                {formatPeriod(report.period, report.period_days, t)}
              </Badge>
              {report.repository?.branch ? (
                <Badge tone="accent" className="font-mono font-normal">
                  {report.repository.branch}
                </Badge>
              ) : null}
            </CardTitle>
            {repoUrl ? (
              <a
                href={repoUrl}
                target="_blank"
                rel="noreferrer"
                className="inline-flex items-center gap-1 text-xs text-accent hover:underline"
              >
                {t('report.openRepository', { platform })}
                <ExternalLink className="size-3" />
              </a>
            ) : null}
          </div>
          <div className="flex shrink-0 items-center gap-1">
            {showSections.show_summary && report.summary ? (
              <CopyButton text={report.summary} label={t('report.copySummary')} />
            ) : null}
          </div>
        </CardHeader>
        <CardContent className="space-y-3">
          {showSections.show_top_labels && report.top_labels ? (
            <div className="flex items-center gap-2">
              <span className="text-xs text-muted">{t('report.labels')}</span>
              <LabelChips labels={report.top_labels} limit={12} />
            </div>
          ) : null}
        </CardContent>
      </Card>

      {/* The mode switcher doubles as the download strip: each tab previews the
          artifact a download would produce, so there is one place to both look
          at an export and take it. */}
      <ReportPreview
        jobId={jobId}
        repo={`${report.owner}/${report.repo}`}
        options={options.data}
        lang={lang}
        leading={repoSelector}
        report={
          <>
            {showSections.show_summary ? (
              <ReportSummary report={report} llmProvider={llmProvider} />
            ) : null}

            <ChartsRow
              report={report}
              showActivity={showSections.show_commits !== false}
              showLanguages={Boolean(showSections.show_languages)}
            />

            <ReportOverview report={report} />

            {report.warnings && report.warnings.length > 0 ? (
              <Card>
                <CardHeader>
                  <CardTitle className="flex items-center gap-2 text-warn">
                    <ReportSectionIcon name="warnings" className="text-warn" />
                    {t('report.warnings')}
                  </CardTitle>
                </CardHeader>
                <CardContent>
                  <WarningList warnings={report.warnings} />
                </CardContent>
              </Card>
            ) : null}

            <Tabs defaultValue={tabs[0]?.key ?? 'summary'}>
              <TabsList>
                {tabs.map((tab) => (
                  <Tab key={tab.key} value={tab.key}>
                    <ReportSectionIcon name={tab.icon} className="size-3.5" />
                    {tab.label}
                    <span className="nums text-[10px] text-muted">{tab.count}</span>
                  </Tab>
                ))}
              </TabsList>

              {showSections.show_prs ? (
                <TabsPanel value="prs">
                  <PullRequestTable report={report} itemLimit={itemLimit} />
                </TabsPanel>
              ) : null}
              {showSections.show_commits ? (
                <TabsPanel value="commits">
                  <CommitTable report={report} itemLimit={itemLimit} />
                </TabsPanel>
              ) : null}
              {showSections.show_issues ? (
                <TabsPanel value="issues">
                  <IssueTable report={report} itemLimit={itemLimit} />
                </TabsPanel>
              ) : null}
              {showSections.show_releases ? (
                <TabsPanel value="releases">
                  <ReleaseTable report={report} itemLimit={itemLimit} />
                </TabsPanel>
              ) : null}
              {showSections.show_contributors ? (
                <TabsPanel value="contributors">
                  <Card>
                    <CardHeader>
                      <CardTitle className="flex items-center gap-2">
                        <ReportSectionIcon name="contributors" />
                        {t('report.contributors')}
                      </CardTitle>
                    </CardHeader>
                    <CardContent>
                      <ContributorList report={report} />
                    </CardContent>
                  </Card>
                </TabsPanel>
              ) : null}
            </Tabs>
          </>
        }
      />
    </div>
  );
}

function ReportSummary({ report, llmProvider }: { report: Report; llmProvider?: string }) {
  const { t } = useTranslation();
  return (
    <Card>
      <CardHeader>
        <CardTitle className="flex items-center gap-2">
          <ReportSectionIcon name="summary" />
          {t('report.summary')}
        </CardTitle>
        <CopyButton text={report.summary} label={t('report.copySummary')} />
      </CardHeader>
      <CardContent>
        <SummaryPanel summary={report.summary} llmProvider={llmProvider} />
      </CardContent>
    </Card>
  );
}

function ChartsRow({
  report,
  showActivity,
  showLanguages,
}: {
  report: Report;
  showActivity: boolean;
  showLanguages: boolean;
}) {
  const { t } = useTranslation();

  return (
    <div className="grid gap-3 lg:grid-cols-3">
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <ReportSectionIcon name="activity" />
            {t('report.activity')}
          </CardTitle>
          <span className="text-xs text-muted">
            {report.commits.total} {t('report.commitsLabel')}
          </span>
        </CardHeader>
        <CardContent>
          {showActivity ? (
            <ActivityChart report={report} />
          ) : (
            <p className="py-8 text-center text-xs text-muted">{t('app.none')}</p>
          )}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <ReportSectionIcon name="weekday" />
            {t('report.weekday')}
          </CardTitle>
        </CardHeader>
        <CardContent>
          {showActivity ? <WeekdayChart report={report} /> : null}
        </CardContent>
      </Card>

      {showLanguages ? (
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <ReportSectionIcon name="languages" />
              {t('report.languages')}
            </CardTitle>
          </CardHeader>
          <CardContent>
            <LanguageChart report={report} />
          </CardContent>
        </Card>
      ) : (
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <ReportSectionIcon name="languages" />
              {t('report.languages')}
            </CardTitle>
          </CardHeader>
          <CardContent>
            <p className="py-8 text-center text-xs text-muted">{t('app.none')}</p>
          </CardContent>
        </Card>
      )}
    </div>
  );
}
