import { zodResolver } from '@hookform/resolvers/zod';
import { useQuery, useQueryClient } from '@tanstack/react-query';
import { useSearch } from '@tanstack/react-router';
import { Play } from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { Controller, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { z } from 'zod';

import { InlineError } from '@/components/ErrorBoundary';
import { type JobRunner } from '@/hooks/useJob';
import { ExportOverrides } from '@/components/run/RunFields';
import { checkExportOverrides } from '@/components/run/runOptions';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Field, Input, Select, Textarea } from '@/components/ui/field';
import { api } from '@/lib/api';
import type { AnalyzeRequest } from '@/lib/types';


/**
 * Every field here has a counterpart in the CLI's Options struct, and the
 * browser-side validation mirrors the C-side ranges so an invalid run is
 * caught before a request is made.
 */
const schema = z
  .object({
    repo: z.string().trim().min(1),
    since_days: z.coerce.number().int().min(1).max(3660),
    from: z.string(),
    to: z.string(),
    platform: z.string(),
    branch: z.string(),
    lang: z.string(),
    api_base: z.string(),
    proxy: z.string(),
    token: z.string(),
    max_pages: z.coerce.number().int().min(1).max(1000),
    llm: z.string(),
    llm_mode: z.string(),
    no_cache: z.boolean(),
    filters: z.string(),
    export_overrides: z.string(),
  })
  .superRefine((value, ctx) => {
    const iso = /^\d{4}-\d{2}-\d{2}$/;
    if (value.from && !iso.test(value.from)) {
      ctx.addIssue({ code: 'custom', path: ['from'], message: 'YYYY-MM-DD' });
    }
    if (value.to && !iso.test(value.to)) {
      ctx.addIssue({ code: 'custom', path: ['to'], message: 'YYYY-MM-DD' });
    }
    // Only meaningful when at least one bound is set, and mirrors the CLI's
    // own ordering check.
    if (value.from && value.to && value.from > value.to) {
      ctx.addIssue({ code: 'custom', path: ['from'], message: 'from > to' });
    }
    // Every override line must be a key=value pair, the same rule the server
    // applies before it stores the override.
    value.export_overrides
      .split('\n')
      .map((line) => line.trim())
      .filter(Boolean)
      .forEach((line) => {
        if (!line.includes('=')) {
          ctx.addIssue({ code: 'custom', path: ['export_overrides'], message: line });
        }
      });
  });

type FormValues = z.infer<typeof schema>;

export function AnalyzeForm({ job }: { job: JobRunner }) {
  const { t } = useTranslation();
  const queryClient = useQueryClient();

  const { data: options } = useQuery({ queryKey: ['meta-options'], queryFn: api.options });
  const { data: config } = useQuery({ queryKey: ['config'], queryFn: api.getConfig });
  const { data: llms } = useQuery({ queryKey: ['llms'], queryFn: api.listLlms });

  const [showAdvanced, setShowAdvanced] = useState(false);

  // The overview page's quick-start box and any shared link arrive as
  // ?repo=..., so the field starts from the URL rather than empty.
  const search = useSearch({ strict: false }) as { repo?: string };
  const urlRepo = typeof search.repo === 'string' ? search.repo.trim() : '';

  const defaults = useMemo<FormValues>(
    () => ({
      repo: urlRepo,
      since_days: config?.default_days ?? 7,
      from: '',
      to: '',
      platform: config?.platform ?? 'github',
      branch: config?.branch ?? '',
      lang: config?.lang ?? 'en',
      api_base: config?.api_base ?? '',
      proxy: config?.proxy ?? '',
      token: '',
      max_pages: config?.max_pages ?? 20,
      llm: '',
      llm_mode: 'health',
      no_cache: false,
      filters: '',
      export_overrides: '',
    }),
    [config, urlRepo],
  );

  const form = useForm<FormValues>({
    resolver: zodResolver(schema),
    defaultValues: defaults,
  });

  // Config values act as the initial defaults; reload them when they arrive but
  // never clobber what the user has already typed.
  useEffect(() => {
    const dirty = Object.keys(form.formState.dirtyFields).length > 0;
    if (!dirty) form.reset(defaults);
  }, [defaults, form]);

  const onSubmit = form.handleSubmit((values) => {
    // An unknown key is ignored by the exporter, so the form names it here
    // rather than letting the setting disappear without a trace.
    const problem = checkExportOverrides(values.export_overrides, options?.export_keys);
    if (problem) {
      form.setError('export_overrides', {
        message:
          problem.kind === 'unknown_key'
            ? `${t('analyze.exportOverridesUnknown')} ${problem.token}`
            : `${t('analyze.exportOverridesInvalid')} ${problem.token}`,
      });
      return;
    }
    // job.start owns the request lifecycle: it reports submission failures
    // through job.error, so a separate mutation would only duplicate the same
    // message in a second place.
    void job.start(async () => {
      const submitted = await api.submitAnalyze(toRequest(values));
      await queryClient.invalidateQueries({ queryKey: ['jobs'] });
      return submitted;
    });
  });

  const errors = form.formState.errors;
  const busy = job.phase === 'submitting' || job.phase === 'running';

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('analyze.title')}</CardTitle>
      </CardHeader>
      <CardContent>
        <form onSubmit={onSubmit} className="space-y-4">
          <Field
            label={t('analyze.repo')}
            hint={t('analyze.repoHint')}
            error={errors.repo?.message}
            htmlFor="repo"
          >
            <Input
              id="repo"
              placeholder={t('analyze.repoPlaceholder')}
              autoComplete="off"
              spellCheck={false}
              invalid={Boolean(errors.repo)}
              {...form.register('repo')}
            />
          </Field>

          <div className="grid gap-4 sm:grid-cols-2">
            <Field label={t('analyze.days', { count: form.watch('since_days') })}>
              <Controller
                control={form.control}
                name="since_days"
                render={({ field }) => (
                  <Input
                    type="number"
                    min={1}
                    max={3660}
                    value={field.value}
                    onChange={field.onChange}
                    onBlur={field.onBlur}
                    ref={field.ref}
                  />
                )}
              />
            </Field>

            <div>
              <p className="mb-1.5 text-xs font-medium text-ink-soft">
                {t('analyze.customRange')}
              </p>
              <div className="grid grid-cols-2 gap-2">
                <Field label={t('analyze.from')} error={errors.from?.message}>
                  <Input type="date" {...form.register('from')} invalid={Boolean(errors.from)} />
                </Field>
                <Field label={t('analyze.to')} error={errors.to?.message}>
                  <Input type="date" {...form.register('to')} invalid={Boolean(errors.to)} />
                </Field>
              </div>
            </div>
          </div>

          <Field label={t('analyze.filters')} hint={t('analyze.filtersHint')}>
            <Textarea
              rows={2}
              placeholder={t('analyze.filtersPlaceholder')}
              spellCheck={false}
              {...form.register('filters')}
            />
          </Field>

          <div className="grid gap-4 sm:grid-cols-2">
            <Field label={t('analyze.platform')}>
              <Select {...form.register('platform')}>
                {(options?.platforms ?? []).map((platform) => (
                  <option key={platform.id} value={platform.id}>
                    {platform.id}
                  </option>
                ))}
              </Select>
            </Field>

            <Field label={t('analyze.branch')}>
              <Input
                placeholder={t('ui.branchPlaceholder')}
                autoComplete="off"
                spellCheck={false}
                {...form.register('branch')}
              />
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
            <div className="space-y-4 rounded-card border border-line bg-raised/50 p-4">
              <div className="grid gap-4 sm:grid-cols-2">
                <Field label={t('analyze.lang')}>
                  <Select {...form.register('lang')}>
                    {(options?.languages ?? []).map((language) => (
                      <option key={language.id} value={language.id}>
                        {language.label}
                      </option>
                    ))}
                  </Select>
                </Field>

                <Field label={t('analyze.maxPages')}>
                  <Input type="number" min={1} {...form.register('max_pages')} />
                </Field>

                <Field label={t('analyze.apiBase')}>
                  <Input autoComplete="off" spellCheck={false} {...form.register('api_base')} />
                </Field>

                <Field label={t('analyze.proxy')}>
                  <Input autoComplete="off" spellCheck={false} {...form.register('proxy')} />
                </Field>

                <Field label={t('analyze.llm')}>
                  <Select {...form.register('llm')}>
                    <option value="">{t('analyze.llmAuto')}</option>
                    <option value="__none__">{t('analyze.llmNone')}</option>
                    {(llms?.llms ?? []).map((provider) => (
                      <option key={provider.name} value={provider.name} disabled={!provider.usable}>
                        {provider.name}
                        {provider.usable ? '' : ` (${t('llm.unusable')})`}
                      </option>
                    ))}
                  </Select>
                </Field>

                <Field label={t('analyze.llmMode')} hint={modeHint(options?.llm_modes, form.getValues('llm_mode'))}>
                  <Select {...form.register('llm_mode')}>
                    {(options?.llm_modes ?? []).map((mode) => (
                      <option key={mode.id} value={mode.id}>
                        {mode.id}
                      </option>
                    ))}
                  </Select>
                </Field>
              </div>

              <Field label={t('analyze.token')} hint={t('analyze.tokenHint')}>
                <Input
                  type="password"
                  autoComplete="off"
                  spellCheck={false}
                  placeholder="ghp_..."
                  {...form.register('token')}
                />
              </Field>

              <Controller
                control={form.control}
                name="export_overrides"
                render={({ field }) => (
                  <ExportOverrides
                    value={field.value}
                    onChange={field.onChange}
                    error={
                      errors.export_overrides?.message
                        ? `${t('analyze.exportOverridesInvalid')} ${errors.export_overrides.message}`
                        : undefined
                    }
                    keys={options?.export_keys}
                  />
                )}
              />

              <label className="flex items-center gap-2 text-sm text-ink-soft">
                <input
                  type="checkbox"
                  className="size-4 accent-[var(--color-accent)]"
                  {...form.register('no_cache')}
                />
                {t('analyze.noCache')}
              </label>
            </div>
          ) : null}

          <div className="flex flex-wrap items-center gap-2">
            <Button type="submit" variant="primary" disabled={busy}>
              <Play />
              {busy ? t('analyze.running') : t('analyze.run')}
            </Button>
            {job.result || job.error ? (
              <Button variant="ghost" onClick={() => job.reset()}>
                {t('app.reset')}
              </Button>
            ) : null}
            {busy ? (
              <Button variant="ghost" onClick={() => void job.cancel()}>
                {t('analyze.cancelJob')}
              </Button>
            ) : null}
          </div>

          {job.error ? <InlineError message={job.error} /> : null}
        </form>
      </CardContent>
    </Card>
  );
}

function modeHint(
  modes: { id: string; hint: string }[] | undefined,
  current: string,
): string | undefined {
  return modes?.find((mode) => mode.id === current)?.hint;
}

/**
 * Flattens form values into a request body. Blank inputs are omitted so the
 * server keeps the config's value; an explicit date range replaces the rolling
 * window, matching how `--from`/`--to` override `--since` on the CLI.
 */
function toRequest(values: FormValues): AnalyzeRequest {
  const body: AnalyzeRequest = {
    repo: values.repo.trim(),
    since_days: values.since_days,
    platform: values.platform,
    max_pages: values.max_pages,
    no_cache: values.no_cache,
    llm_mode: values.llm_mode,
    lang: values.lang,
  };
  if (values.from) body.from = values.from;
  if (values.to) body.to = values.to;
  if (values.branch.trim()) body.branch = values.branch.trim();
  if (values.api_base.trim()) body.api_base = values.api_base.trim();
  if (values.proxy.trim()) body.proxy = values.proxy.trim();
  if (values.token.trim()) body.token = values.token.trim();
  if (values.llm === '__none__') body.suppress_llm = true;
  else if (values.llm) body.llm = values.llm;

  const filters = values.filters
    .split(/[\n,]+/)
    .map((entry) => entry.trim())
    .filter(Boolean);
  if (filters.length > 0) body.filters = filters;

  // The CLI's repeatable `--export key=value`; the server accepts the same
  // pairs and applies them to this run's rendering only.
  const overrides = values.export_overrides
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean);
  if (overrides.length > 0) body.export = overrides;

  return body;
}