import { useQuery } from '@tanstack/react-query';
import { Play } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';

import { InlineError } from '@/components/ErrorBoundary';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Field, Input, Select } from '@/components/ui/field';
import { api } from '@/lib/api';

import { AnalysisProgress } from '@/features/analyze/AnalysisProgress';
import { useJob } from '@/hooks/useJob';

/**
 * Runs every subscription as one job, with cross-repository parallelism. Each
 * entry keeps its own window, branch and platform, so this is the closest
 * equivalent to `gs watch analyze` from the CLI.
 */
export function SubscriptionBatchPanel() {
  const { t } = useTranslation();
  const job = useJob();

  const { data: subscriptions } = useQuery({
    queryKey: ['subscriptions'],
    queryFn: api.listSubscriptions,
  });
  const { data: options } = useQuery({ queryKey: ['meta-options'], queryFn: api.options });
  const { data: llms } = useQuery({ queryKey: ['llms'], queryFn: api.listLlms });

  const maxThreads = options?.batch_max_threads ?? 8;
  const [parallel, setParallel] = useState(4);
  const [llm, setLlm] = useState('');
  const [llmMode, setLlmMode] = useState('');
  const [lang, setLang] = useState('');
  const [noCache, setNoCache] = useState(false);

  const count = subscriptions?.subscriptions.length ?? 0;
  const busy = job.phase === 'submitting' || job.phase === 'running';

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('subs.analyzeAll')}</CardTitle>
        <span className="nums text-xs text-muted">
          {count} {t('subs.title')}
        </span>
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="grid items-end gap-4 sm:grid-cols-[10rem_1fr_1fr_auto]">
          <Field label={t('batch.parallel')} hint={t('batch.parallelHint', { max: maxThreads })}>
            <Input
              type="number"
              min={1}
              max={maxThreads}
              value={parallel}
              onChange={(event) =>
                setParallel(Math.min(maxThreads, Math.max(1, Number(event.target.value) || 1)))
              }
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

          <Field label={t('analyze.llmMode')}>
            <Select value={llmMode} onChange={(event) => setLlmMode(event.target.value)}>
              <option value="">{t('analyze.llmAuto')}</option>
              {(options?.llm_modes ?? []).map((mode) => (
                <option key={mode.id} value={mode.id}>
                  {mode.id}
                </option>
              ))}
            </Select>
          </Field>

          <Button
            variant="primary"
            disabled={busy || count === 0}
            onClick={() =>
              void job.start(() =>
                api.analyzeSubscriptions({
                  parallel,
                  llm: llm || undefined,
                  llm_mode: llmMode || undefined,
                  lang: lang || undefined,
                  no_cache: noCache || undefined,
                }),
              )
            }
          >
            <Play />
            {busy ? t('subs.analyzingAll') : t('subs.analyzeAll')}
          </Button>
        </div>

        <div className="grid items-end gap-4 sm:grid-cols-[1fr_auto]">
          <Field label={t('analyze.lang')}>
            <Select value={lang} onChange={(event) => setLang(event.target.value)}>
              <option value="">{t('analyze.llmAuto')}</option>
              {(options?.languages ?? []).map((language) => (
                <option key={language.id} value={language.id}>
                  {language.label}
                </option>
              ))}
            </Select>
          </Field>

          <label className="flex items-center gap-2 pb-2 text-sm text-ink-soft">
            <input
              type="checkbox"
              className="size-4 accent-[var(--color-accent)]"
              checked={noCache}
              onChange={(event) => setNoCache(event.target.checked)}
            />
            {t('analyze.noCache')}
          </label>
        </div>

        {job.error ? <InlineError message={job.error} /> : null}

        {job.phase !== 'idle' ? (
          <AnalysisProgress
            progress={job.progress}
            phase={job.phase}
            onCancel={() => void job.cancel()}
          />
        ) : null}

        {job.result && job.id && 'reports' in job.result ? (
          <p className="text-xs text-muted">
            {t('batch.summary', {
              ok: job.result.reports.filter((entry) => entry.status === 0).length,
              total: job.result.reports.length,
            })}
          </p>
        ) : null}
      </CardContent>
    </Card>
  );
}