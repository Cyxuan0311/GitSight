import { useQuery } from '@tanstack/react-query';
import { ArrowDown, ArrowRight, ArrowUp, GitCompareArrows, Minus } from 'lucide-react';
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
import { Field, Input, Select } from '@/components/ui/field';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import { SummaryPanel } from '@/components/report/ReportParts';
import { api } from '@/lib/api';
import { cn } from '@/lib/cn';
import { compareRows, splitTop } from '@/lib/format';
import { isCompareResult } from '@/lib/types';

import { AnalysisProgress } from '@/features/analyze/AnalysisProgress';
import { useJob } from '@/hooks/useJob';

export function ComparePage() {
  const { t } = useTranslation();
  const job = useJob();

  const { data: config } = useQuery({ queryKey: ['config'], queryFn: api.getConfig });
  const { data: llms } = useQuery({ queryKey: ['llms'], queryFn: api.listLlms });
  const { data: options } = useQuery({ queryKey: ['meta-options'], queryFn: api.options });

  const [repo, setRepo] = useState('');
  const [daysA, setDaysA] = useState(7);
  const [daysB, setDaysB] = useState(30);
  const [llm, setLlm] = useState('');
  const [showAdvanced, setShowAdvanced] = useState(false);
  const [advanced, setAdvanced] = useState<RunOptionValues>(emptyRunOptions);
  const [formError, setFormError] = useState<string | undefined>();

  const result = job.result && isCompareResult(job.result) ? job.result : null;

  const run = () => {
    const trimmed = repo.trim();
    if (!trimmed) return;
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
    void job.start(() =>
      api.submitCompare({
        repo: trimmed,
        days_a: daysA,
        days_b: daysB,
        llm: llm || undefined,
        no_cache: false,
        ...runOptionsToRequest(advanced),
      }),
    );
  };

  const busy = job.phase === 'submitting' || job.phase === 'running';

  return (
    <AppLayout path="/compare" title={t('compare.title')} subtitle={t('compare.subtitle')}>
      <div className="space-y-4">
        <Card>
          <CardContent className="pt-4">
            <div className="space-y-4">
              <div className="grid items-end gap-4 lg:grid-cols-[2fr_1fr_1fr_1fr_auto]">
                <Field label={t('compare.repo')}>
                  <Input
                    value={repo}
                    onChange={(event) => setRepo(event.target.value)}
                    onKeyDown={(event) => {
                      if (event.key === 'Enter') run();
                    }}
                    placeholder={t('analyze.repoPlaceholder')}
                    autoComplete="off"
                    spellCheck={false}
                  />
                </Field>

                <Field label={t('compare.periodA')}>
                  <Input
                    type="number"
                    min={1}
                    value={daysA}
                    onChange={(event) => setDaysA(Math.max(1, Number(event.target.value) || 1))}
                    disabled={daysA >= daysB}
                    title={daysA >= daysB ? 'A must be shorter than B' : undefined}
                  />
                </Field>

                <Field label={t('compare.periodB')}>
                  <Input
                    type="number"
                    min={1}
                    value={daysB}
                    onChange={(event) => setDaysB(Math.max(1, Number(event.target.value) || 1))}
                  />
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

                <Button variant="primary" onClick={run} disabled={busy || !repo.trim()}>
                  <GitCompareArrows />
                  {busy ? t('analyze.running') : t('compare.run')}
                </Button>
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

        {result ? (
          <CompareResult
            result={result}
            jobId={job.id}
            platform={config?.platform ?? 'github'}
          />
        ) : job.phase === 'idle' ? (
          <div className="rounded-card border border-dashed border-line bg-surface">
            <EmptyState
              icon={<GitCompareArrows />}
              title={t('compare.title')}
              hint={t('compare.subtitle')}
            />
          </div>
        ) : null}
      </div>
    </AppLayout>
  );
}

function CompareResult({
  result,
  jobId,
  platform,
}: {
  result: import('@/lib/types').CompareResult;
  jobId: string;
  platform: string;
}) {
  const { t } = useTranslation();
  const { rows, hours } = compareRows(result);
  const a = result.period_a;
  const b = result.period_b;

  return (
    <div className="space-y-4">
      <Card>
        <CardHeader>
          <CardTitle className="text-base">
            {result.owner}/{result.repo}
          </CardTitle>
          <div className="flex items-center gap-2">
            <Badge tone="neutral" className="nums font-normal">
              {t('compare.periodA')} · {result.period_days_a}d
            </Badge>
            <ArrowRight className="size-3.5 text-muted" />
            <Badge tone="accent" className="nums font-normal">
              {t('compare.periodB')} · {result.period_days_b}d
            </Badge>
          </div>
        </CardHeader>
        <CardContent className="px-0 pb-0">
          <Table>
            <thead>
              <tr>
                <Th>{t('compare.metric')}</Th>
                <Th className="w-24 text-right">
                  {t('compare.periodA')} ({result.period_days_a}d)
                </Th>
                <Th className="w-24 text-right">
                  {t('compare.periodB')} ({result.period_days_b}d)
                </Th>
                <Th className="w-24 text-right">{t('compare.delta')}</Th>
              </tr>
            </thead>
            <tbody>
              {rows.map((row) => (
                <tr key={row.key} className="hover:bg-raised/60">
                  <Td className="text-ink">{t(row.labelKey)}</Td>
                  <Td className="nums text-right">{row.a}</Td>
                  <Td className="nums text-right">{row.b}</Td>
                  <Td className="text-right">
                    <DeltaCell delta={row.delta} />
                  </Td>
                </tr>
              ))}
              {hours.map((row) => (
                <tr key={row.key} className="hover:bg-raised/60">
                  <Td className="text-ink">{t(row.labelKey)}</Td>
                  <Td className="nums text-right">{row.format(row.a)}</Td>
                  <Td className="nums text-right">{row.format(row.b)}</Td>
                  <Td className="text-right">
                    <DeltaCell delta={row.delta} />
                  </Td>
                </tr>
              ))}
            </tbody>
          </Table>
        </CardContent>
      </Card>

      <div className="grid gap-3 lg:grid-cols-3">
        <NarrativeCard label={t('compare.topAuthor')} a={splitTop(a.top_author).name} b={splitTop(b.top_author).name} />
        <NarrativeCard
          label={t('compare.topContributor')}
          a={splitTop(a.top_contributor).name}
          b={splitTop(b.top_contributor).name}
        />
        <NarrativeCard
          label={t('compare.busiestDay')}
          a={busiest(a.activity)}
          b={busiest(b.activity)}
        />
      </div>

      <Card>
        <CardHeader>
          <CardTitle>{t('compare.trend')}</CardTitle>
        </CardHeader>
        <CardContent>
          {result.trend_summary ? (
            <SummaryPanel summary={result.trend_summary} llmProvider={result.llm_provider} />
          ) : (
            <EmptyState className="py-6" title={t('compare.noTrend')} />
          )}
        </CardContent>
      </Card>

      <CompareExports jobId={jobId} repo={`${result.owner}/${result.repo}`} platform={platform} />
    </div>
  );
}

function DeltaCell({ delta }: { delta: import('@/lib/format').Delta }) {
  const Icon =
    delta.direction === 'up' ? ArrowUp : delta.direction === 'down' ? ArrowDown : Minus;
  return (
    <span
      className={cn(
        'nums inline-flex items-center gap-1 text-xs font-medium',
        delta.direction === 'up' && 'text-open',
        delta.direction === 'down' && 'text-closed',
        delta.direction === 'flat' && 'text-muted',
        delta.direction === 'unknown' && 'text-muted',
      )}
    >
      <Icon className="size-3" />
      {delta.label}
    </span>
  );
}

function NarrativeCard({ label, a, b }: { label: string; a: string; b: string }) {
  return (
    <Card>
      <CardHeader>
        <CardTitle className="text-xs text-muted">{label}</CardTitle>
      </CardHeader>
      <CardContent>
        <div className="flex items-center gap-2 text-sm">
          <span className="min-w-0 flex-1 truncate text-ink">{a || '-'}</span>
          <ArrowRight className="size-3.5 shrink-0 text-muted" />
          <span className="min-w-0 flex-1 truncate text-right text-ink">{b || '-'}</span>
        </div>
      </CardContent>
    </Card>
  );
}

/** The busiest bucket label from an activity map, or a dash when empty. */
function busiest(activity: Record<string, number>): string {
  const entries = Object.entries(activity);
  if (entries.length === 0) return '-';
  const [label, count] = entries.reduce((best, entry) => (entry[1] > best[1] ? entry : best));
  return count > 0 ? `${label} (${count})` : '-';
}

function CompareExports({
  jobId,
  repo,
  platform,
}: {
  jobId: string;
  repo: string;
  platform: string;
}) {
  const { t } = useTranslation();
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  // A comparison is only ever markdown or json, so this strip is narrower than
  // the report view's and lists exactly what applies.
  const formats = ['json', 'md'];

  const download = async (format: string) => {
    setBusy(format);
    setError(null);
    try {
      await api.downloadExportFor(jobId, format, repo);
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : t('errors.generic'));
    } finally {
      setBusy(null);
    }
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('report.export')}</CardTitle>
      </CardHeader>
      <CardContent className="space-y-2">
        <div className="flex flex-wrap items-center gap-2">
          {formats.map((format) => (
            <Button
              key={format}
              variant="outline"
              size="sm"
              disabled={busy !== null}
              onClick={() => void download(format)}
            >
              {format.toUpperCase()}
            </Button>
          ))}
          <span className="text-xs text-muted">
            {platform} · {t('report.exportHint')}
          </span>
        </div>
        {error ? <p className="text-xs text-danger">{error}</p> : null}
      </CardContent>
    </Card>
  );
}