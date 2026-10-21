import { useNavigate, useSearch } from '@tanstack/react-router';
import { Activity, GitCompareArrows, Star } from 'lucide-react';
import { useTranslation } from 'react-i18next';

import { AppLayout } from '@/components/layout/AppLayout';
import { ApiErrorBoundary } from '@/components/ErrorBoundary';
import { EmptyState } from '@/components/ui/feedback';
import { ReportView } from '@/components/report/ReportView';
import { useReportSettings } from '@/components/report/useReportSettings';
import { Button } from '@/components/ui/button';
import { ResizableSplit } from '@/components/ui/resizable';
import { isBatchResult, isCompareResult } from '@/lib/types';

import { AnalyzeForm } from '@/features/analyze/AnalyzeForm';
import { AnalysisProgress } from '@/features/analyze/AnalysisProgress';
import { useJob } from '@/hooks/useJob';

export function AnalyzePage() {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const settings = useReportSettings();

  /* One runner owns the run, and it is this page's. The form submits through
   * this instance, so the progress and report rendered below always describe the
   * run the form started. `?job=` adopts a run that is already in flight, which
   * is what makes the overview's history and a subscription's "analyze now"
   * land on a live run instead of an empty page. */
  const search = useSearch({ strict: false }) as { job?: string; repo?: string };
  const job = useJob(typeof search.job === 'string' ? search.job : undefined);


  // A compare payload belongs on the compare page; a batch payload would need
  // a repo picker. Neither is reachable from this form, so anything else is a
  // plain single-repository report.
  const report =
    job.result && !isBatchResult(job.result) && !isCompareResult(job.result)
      ? job.result
      : null;

  return (
    <AppLayout path="/analyze" title={t('analyze.title')} subtitle={t('analyze.subtitle')}>
      <ApiErrorBoundary>
        <ResizableSplit
          storageKey="gitsight.analyze.sideWidth"
          defaultWidth={344}
          side={
            <div className="space-y-4 pr-2">
              <AnalyzeForm job={job} />
              {job.phase !== 'idle' ? (
                <AnalysisProgress
                  progress={job.progress}
                  phase={job.phase}
                  onCancel={() => void job.cancel()}
                />
              ) : null}
            </div>
          }
        >
          {report && job.id ? (
            <ReportView
              report={report}
              jobId={job.id}
              platform={settings.platform}
              itemLimit={settings.itemLimit}
              showSections={settings.showSections}
              lang={settings.lang}
            />
          ) : job.phase === 'idle' ? (
            <div className="rounded-card border border-dashed border-line bg-surface">
              <EmptyState
                icon={<Activity />}
                title={t('analyze.title')}
                hint={t('analyze.selectPrompt')}
                action={
                  <div className="flex flex-wrap justify-center gap-2">
                    <Button variant="ghost" size="sm" onClick={() => void navigate({ to: '/compare' })}>
                      <GitCompareArrows />
                      {t('analyze.compareHint')}
                    </Button>
                    <Button variant="ghost" size="sm" onClick={() => void navigate({ to: '/subscriptions' })}>
                      <Star />
                      {t('analyze.subscribeHint')}
                    </Button>
                  </div>
                }
              />
            </div>
          ) : null}
        </ResizableSplit>
      </ApiErrorBoundary>
    </AppLayout>
  );
}