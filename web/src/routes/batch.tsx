import { useQuery } from '@tanstack/react-query';
import { Layers, ListChecks, Play } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';

import { AppLayout } from '@/components/layout/AppLayout';
import { InlineError } from '@/components/ErrorBoundary';
import { AdvancedRunFields } from '@/components/run/RunFields';
import {
  checkExportOverrides,
  emptyRunOptions,
  runOptionsToRequest,
  type RunOptionValues,
} from '@/components/run/runOptions';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Field, Input, Select, Textarea } from '@/components/ui/field';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import { ExportBar } from '@/components/common';
import { api } from '@/lib/api';
import { isBatchResult, type BatchResult, type Report } from '@/lib/types';
import { batchOkCount } from '@/lib/format';

import { AnalysisProgress } from '@/features/analyze/AnalysisProgress';
import { BatchStatus } from '@/components/report/ReportParts';
import { ReportView } from '@/components/report/ReportView';
import { useReportSettings } from '@/components/report/useReportSettings';
import { useJob } from '@/hooks/useJob';

export function BatchPage() {
  const { t } = useTranslation();
  const job = useJob();
  const settings = useReportSettings();

  const { data: options } = useQuery({ queryKey: ['meta-options'], queryFn: api.options });
  const { data: config } = useQuery({ queryKey: ['config'], queryFn: api.getConfig });
  const { data: llms } = useQuery({ queryKey: ['llms'], queryFn: api.listLlms });
  const maxRepos = options?.max_repos ?? 16;
  const maxThreads = options?.batch_max_threads ?? 8;

  const [text, setText] = useState('');
  const [parallel, setParallel] = useState(4);
  const [sinceDays, setSinceDays] = useState(config?.default_days ?? 7);
  const [from, setFrom] = useState('');
  const [to, setTo] = useState('');
  const [llm, setLlm] = useState('');
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [advanced, setAdvanced] = useState<RunOptionValues>(emptyRunOptions);
  const [formError, setFormError] = useState<string | undefined>();

  // Blank lines and commas both separate, matching how the server splits them.
  const repos = Array.from(
    new Set(
      text
        .split(/[\n,]+/)
        .map((entry) => entry.trim())
        .filter(Boolean),
    ),
  );

  const result: BatchResult | null = job.result && isBatchResult(job.result) ? job.result : null;
  const busy = job.phase === 'submitting' || job.phase === 'running';

  const [selected, setSelected] = useState<string | null>(null);
  const active: Report | null =
    result?.reports.find((entry) => entry.repo === selected)?.report ??
    (result?.reports.length === 1 ? result.reports[0]!.report : null);

  const run = () => {
    if (repos.length === 0) return;
    // A malformed or unknown override would be rejected or ignored by the
    // server; catching it here names the offending line instead of failing
    // the whole request.
    const problem = checkExportOverrides(advanced.exportOverrides, options?.export_keys);
    if (problem) {
      setFormError(
        `${t(problem.kind === 'unknown_key' ? 'analyze.exportOverridesUnknown' : 'analyze.exportOverridesInvalid')} ${problem.token}`,
      );
      return;
    }
    setFormError(undefined);
    setSelected(null);
    void job.start(() =>
      api.submitAnalyze({
        repos: repos.slice(0, maxRepos),
        parallel,
        since_days: sinceDays,
        from: from || undefined,
        to: to || undefined,
        llm: llm || undefined,
        ...runOptionsToRequest(advanced),
      }),
    );
  };

  return (
    <AppLayout path="/batch" title={t('batch.title')} subtitle={t('batch.subtitle')}>
      <div className="space-y-4">
        <Card>
          <CardContent className="pt-4">
            <div className="space-y-4">
              <div className="grid gap-4 lg:grid-cols-[1fr_12rem_auto]">
                <Field
                  label={t('batch.repos')}
                  hint={`${t('batch.reposHint', { max: maxRepos })} · ${t('batch.duplicate')}`}
                >
                  <Textarea
                    value={text}
                    onChange={(event) => setText(event.target.value)}
                    onKeyDown={(event) => {
                      if ((event.metaKey || event.ctrlKey) && event.key === 'Enter') run();
                    }}
                    rows={6}
                    placeholder={t('batch.reposPlaceholder')}
                    spellCheck={false}
                    className="font-mono text-xs"
                  />
                </Field>

                <div className="space-y-4">
                  <Field label={t('batch.parallel')} hint={t('batch.parallelHint', { max: maxThreads })}>
                    <Input
                      type="number"
                      min={1}
                      max={maxThreads}
                      value={parallel}
                      onChange={(event) =>
                        setParallel(
                          Math.min(maxThreads, Math.max(1, Number(event.target.value) || 1)),
                        )
                      }
                    />
                  </Field>
                  <p className="nums text-xs text-muted">
                    {repos.length} / {maxRepos}
                  </p>
                </div>

                <div className="flex items-end">
                  <Button
                    variant="primary"
                    onClick={run}
                    disabled={busy || repos.length === 0}
                    className="w-full lg:w-auto"
                  >
                    <Play />
                    {busy ? t('analyze.runningAll', { count: repos.length }) : t('batch.run')}
                  </Button>
                </div>
              </div>

              <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-4">
                <Field label={t('analyze.window')}>
                  <Input
                    type="number"
                    min={1}
                    max={3660}
                    value={sinceDays}
                    onChange={(event) =>
                      setSinceDays(Math.max(1, Number(event.target.value) || 1))
                    }
                  />
                </Field>

                <Field label={t('analyze.from')}>
                  <Input
                    type="date"
                    value={from}
                    onChange={(event) => setFrom(event.target.value)}
                  />
                </Field>

                <Field label={t('analyze.to')}>
                  <Input type="date" value={to} onChange={(event) => setTo(event.target.value)} />
                </Field>

                <Field label={t('analyze.llm')}>
                  <Select value={llm} onChange={(event) => setLlm(event.target.value)}>
                    <option value="">{t('analyze.llmAuto')}</option>
                    {(llms?.llms ?? []).map((provider) => (
                      <option key={provider.name} value={provider.name} disabled={!provider.usable}>
                        {provider.name}
                      </option>
                    ))}
                  </Select>
                </Field>
              </div>

              <button
                type="button"
                onClick={() => setShowAdvanced((prev) => !prev)}
                className="text-xs font-medium text-accent hover:underline"
              >
                {t('analyze.advanced')}
              </button>

              {showAdvanced ? (
                <AdvancedRunFields
                  value={advanced}
                  onChange={setAdvanced}
                  options={options}
                  exportError={formError}
                />
              ) : null}
            </div>
          </CardContent>
        </Card>

        {job.error ? <InlineError message={job.error} /> : null}

        {job.phase !== 'idle' ? (
          <AnalysisProgress
            progress={job.progress}
            phase={job.phase}
            onCancel={() => void job.cancel()}
          />
        ) : null}

        {result && job.id ? (
          <>
            <Card>
              <CardHeader>
                <CardTitle>
                  {t('batch.summary', { ok: batchOkCount(result), total: result.total })}
                </CardTitle>
                <Badge tone={result.failed > 0 ? 'warn' : 'ok'}>
                  {result.ok} {t('batch.ok')} · {result.failed} {t('batch.failed')}
                </Badge>
              </CardHeader>
              <CardContent className="px-0 pb-0">
                {result.reports.length === 0 ? (
                  <EmptyState className="py-8" title={t('app.empty')} />
                ) : (
                  <Table>
                    <thead>
                      <tr>
                        <Th className="w-16">#</Th>
                        <Th>{t('batch.repos')}</Th>
                        <Th className="w-24">{t('batch.results')}</Th>
                        <Th className="w-32 text-right">{t('report.export')}</Th>
                      </tr>
                    </thead>
                    <tbody>
                      {result.reports.map((entry, index) => (
                        <tr
                          key={entry.repo}
                          className={`hover:bg-raised/60 ${selected === entry.repo ? 'bg-accent-soft/40' : ''}`}
                        >
                          <Td className="nums text-muted">{index + 1}</Td>
                          <Td className="font-mono text-xs text-ink">{entry.repo}</Td>
                          <Td>
                            <BatchStatus entry={entry} />
                          </Td>
                          <Td className="text-right">
                            <div className="flex justify-end gap-1">
                              <Button
                                variant="ghost"
                                size="sm"
                                onClick={() =>
                                  setSelected(selected === entry.repo ? null : entry.repo)
                                }
                              >
                                {t('batch.viewReport')}
                              </Button>
                              {entry.status === 0 ? (
                                <Button
                                  variant="outline"
                                  size="sm"
                                  onClick={() =>
                                    void api
                                      .downloadExportFor(job.id, 'pdf', entry.repo)
                                      .catch(() => undefined)
                                  }
                                >
                                  {t('batch.download')}
                                </Button>
                              ) : null}
                            </div>
                          </Td>
                        </tr>
                      ))}
                    </tbody>
                  </Table>
                )}
              </CardContent>
            </Card>

            {result.reports.length > 1 ? (
              <p className="flex items-center gap-1.5 text-xs text-muted">
                <Layers className="size-3.5" />
                {t('batch.pickRepo')}
              </p>
            ) : null}

            {active && selected !== null || (active && result.reports.length === 1) ? (
              <ReportView
                report={active!}
                jobId={job.id}
                platform={settings.platform}
                itemLimit={settings.itemLimit}
                showSections={settings.showSections}
                lang={settings.lang}
              />
            ) : null}
          </>
        ) : job.phase === 'idle' ? (
          <div className="rounded-card border border-dashed border-line bg-surface">
            <EmptyState icon={<ListChecks />} title={t('batch.title')} hint={t('batch.subtitle')} />
          </div>
        ) : null}
      </div>
    </AppLayout>
  );
}

export { ExportBar };