import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { FileJson, RotateCcw, Save } from 'lucide-react';
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';

import { AppLayout } from '@/components/layout/AppLayout';
import { InlineError, QueryState } from '@/components/ErrorBoundary';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Field, Input, Select } from '@/components/ui/field';
import { CodeTextarea } from '@/components/ui/code-block';
import { ConfirmDialog, Modal } from '@/components/ui/modal';
import { ColorField } from '@/features/settings/ColorField';
import { toast } from '@/stores/toast';
import { SubscriptionSettings } from '@/features/settings/SubscriptionSettings';
import { api } from '@/lib/api';
import { syncLanguage } from '@/lib/language';
import { cn } from '@/lib/cn';
import type { ConfigResponse } from '@/lib/types';

type Draft = Record<string, string | number | boolean>;

/**
 * The settings form is a flat bag of `key -> value` pairs sent to
 * `PATCH /api/config`, which the server applies through config_set(). Keeping
 * one shape for every control means the browser cannot write a key the C-side
 * whitelist would reject.
 */
export function SettingsPage() {
  const { t } = useTranslation();
  const queryClient = useQueryClient();

  const config = useQuery({ queryKey: ['config'], queryFn: api.getConfig });
  const options = useQuery({ queryKey: ['meta-options'], queryFn: api.options });

  const [draft, setDraft] = useState<Draft>({});
  const [tokens, setTokens] = useState<Record<string, string>>({});
  const [rawOpen, setRawOpen] = useState(false);
  const [resetting, setResetting] = useState(false);

  // Reset the draft whenever fresh config arrives, so switching pages and back
  // always shows what is actually stored.
  useEffect(() => {
    if (!config.data) return;
    setDraft(toDraft());
    setTokens({});
  }, [config.data]);

  const save = useMutation({
    mutationFn: (sets: Draft) => api.patchConfig(sets),
    onSuccess: () => {
      toast.success(t('settings.saved'));
      void queryClient.invalidateQueries({ queryKey: ['config'] });
      void queryClient.invalidateQueries({ queryKey: ['subscriptions'] });
      setDraft({});
      setTokens({});
    },
    onError: (cause) =>
      toast.error(cause instanceof Error ? cause.message : t('errors.generic')),
  });

  const reset = useMutation({
    mutationFn: api.resetConfig,
    onSuccess: () => {
      toast.success(t('settings.resetDone'));
      void queryClient.invalidateQueries({ queryKey: ['config'] });
    },
    onError: (cause) =>
      toast.error(cause instanceof Error ? cause.message : t('errors.generic')),
  });

  const set = (key: string, value: string | number | boolean) =>
    setDraft((prev) => ({ ...prev, [key]: value }));

  const data = config.data;
  const changed = Object.keys(draft).length > 0 || Object.values(tokens).some(Boolean);
  const value = <K extends keyof ConfigResponse>(key: K): ConfigResponse[K] | undefined =>
    (data?.[key]) as ConfigResponse[K] | undefined;

  const busy = save.isPending;
  const exportSettings = data?.export;

  return (
    <AppLayout path="/settings" title={t('settings.title')} subtitle={t('settings.subtitle')}>
      <QueryState query={config} rows={3}>
        {data ? (
          <form
            className="space-y-4"
            onSubmit={(event) => {
              event.preventDefault();
              if (!changed) return;
              const sets: Draft = { ...draft };
              for (const [key, token] of Object.entries(tokens)) {
                // An empty token field means "leave the stored value alone"; a
                // special clear marker is sent to actually remove it.
                if (token === '') continue;
                sets[key] = token === '__clear__' ? '' : token;
              }
              save.mutate(sets);
            }}
          >
            {/* ---------------------------------------------------- general */}
            <Card>
              <CardHeader>
                <CardTitle>{t('settings.general')}</CardTitle>
              </CardHeader>
              <CardContent className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
                <Field label={t('settings.platform')} hint={t('settings.platformHint')}>
                  <Select
                    value={String(draft.platform ?? value('platform') ?? 'github')}
                    onChange={(event) => set('platform', event.target.value)}
                  >
                    {(options.data?.platforms ?? []).map((platform) => (
                      <option key={platform.id} value={platform.id}>
                        {platform.id}
                      </option>
                    ))}
                  </Select>
                </Field>

                <Field label={t('settings.lang')}>
                  {/* The report language and the console language are one
                      setting, so changing it here switches the interface too. */}
                  <Select
                    value={String(draft.lang ?? value('lang') ?? 'en')}
                    onChange={(event) => {
                      const next = event.target.value;
                      set('lang', next);
                      if (next === 'en' || next === 'zh') syncLanguage(next);
                    }}
                  >
                    {(options.data?.languages ?? []).map((language) => (
                      <option key={language.id} value={language.id}>
                        {language.label}
                      </option>
                    ))}
                  </Select>
                </Field>

                <Field label={t('settings.defaultDays')}>
                  <Input
                    type="number"
                    min={1}
                    value={Number(draft.default_days ?? value('default_days') ?? 7)}
                    onChange={(event) => set('default_days', Number(event.target.value))}
                  />
                </Field>

                <Field label={t('settings.maxPages')}>
                  <Input
                    type="number"
                    min={1}
                    value={Number(draft.max_pages ?? value('max_pages') ?? 20)}
                    onChange={(event) => set('max_pages', Number(event.target.value))}
                  />
                </Field>

                <Field label={t('settings.format')}>
                  <Select
                    value={String(draft.format ?? value('format') ?? 'md')}
                    onChange={(event) => set('format', event.target.value)}
                  >
                    {(options.data?.formats ?? []).map((format) => (
                      <option key={format.id} value={format.id}>
                        {format.id}
                      </option>
                    ))}
                  </Select>
                </Field>

                <Field label={t('settings.color')}>
                  <Select
                    value={String(draft.color ?? value('color') ?? 'auto')}
                    onChange={(event) => set('color', event.target.value)}
                  >
                    <option value="auto">{t('settings.colorAuto')}</option>
                    <option value="always">{t('settings.colorAlways')}</option>
                    <option value="never">{t('settings.colorNever')}</option>
                  </Select>
                </Field>

                <Field label={t('settings.branch')}>
                  <Input
                    value={String(draft.branch ?? value('branch') ?? '')}
                    onChange={(event) => set('branch', event.target.value)}
                    placeholder={t('ui.branchPlaceholder')}
                    spellCheck={false}
                  />
                </Field>

                <Field label={t('settings.apiBase')} className="sm:col-span-2">
                  <Input
                    value={String(draft.api_base ?? value('api_base') ?? '')}
                    onChange={(event) => set('api_base', event.target.value)}
                    spellCheck={false}
                  />
                </Field>

                <Field label={t('settings.proxy')} hint={t('settings.proxyHint')} className="sm:col-span-2">
                  <Input
                    value={String(draft.proxy ?? value('proxy') ?? '')}
                    onChange={(event) => set('proxy', event.target.value)}
                    placeholder="http://127.0.0.1:7890"
                    spellCheck={false}
                  />
                </Field>
              </CardContent>
            </Card>

            {/* ----------------------------------------------------- tokens */}
            <Card>
              <CardHeader>
                <CardTitle>{t('settings.tokens')}</CardTitle>
                <span className="text-xs text-muted">{t('settings.tokensHint')}</span>
              </CardHeader>
              <CardContent className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
                <SecretField
                  label={t('settings.githubToken')}
                  stored={data.github_token}
                  value={tokens.github_token ?? ''}
                  onChange={(next) => setTokens((prev) => ({ ...prev, github_token: next }))}
                />
                <SecretField
                  label={t('settings.gitlabToken')}
                  stored={data.gitlab_token}
                  value={tokens.gitlab_token ?? ''}
                  onChange={(next) => setTokens((prev) => ({ ...prev, gitlab_token: next }))}
                />
                <SecretField
                  label={t('settings.codebergToken')}
                  stored={data.codeberg_token}
                  value={tokens.codeberg_token ?? ''}
                  onChange={(next) => setTokens((prev) => ({ ...prev, codeberg_token: next }))}
                />
              </CardContent>
            </Card>

            {/* ---------------------------------------------------- sections */}
            <Card>
              <CardHeader>
                <div>
                  <CardTitle>{t('settings.reportSections')}</CardTitle>
                  <p className="text-xs text-muted">{t('settings.webOnly')}</p>
                </div>
                <Badge tone="accent">{t('settings.scope_web')}</Badge>
              </CardHeader>
              <CardContent>
                <SectionToggles settings={exportSettings} draft={draft} set={set} />
                <div className="mt-4 max-w-48">
                  <Field label={t('settings.itemLimit')} hint={t('settings.itemLimitHint')}>
                    <Input
                      type="number"
                      min={2}
                      max={10}
                      value={Number(
                        draft['export.item_limit'] ?? exportSettings?.item_limit ?? 10,
                      )}
                      onChange={(event) => set('export.item_limit', Number(event.target.value))}
                    />
                  </Field>
                </div>
              </CardContent>
            </Card>

            {/* ---------------------------------------------------- advanced */}
            <AdvancedExport
              settings={exportSettings}
              draft={draft}
              set={set}
              options={options.data}
            />

            {save.isError ? (
              <InlineError
                message={save.error instanceof Error ? save.error.message : t('errors.generic')}
              />
            ) : null}
            {save.data?.rejected ? (
              <p className="text-xs text-warn">{t('settings.rejected')}</p>
            ) : null}

            <SubscriptionSettings />

            <div className="sticky bottom-0 flex flex-wrap items-center gap-2 border-t border-line bg-canvas/90 py-3 backdrop-blur">
              <Button type="submit" variant="primary" disabled={!changed || busy}>
                <Save />
                {busy ? t('app.saving') : t('app.save')}
              </Button>
              <Button
                variant="ghost"
                disabled={!changed || busy}
                onClick={() => {
                  setDraft({});
                  setTokens({});
                }}
              >
                {t('app.reset')}
              </Button>
              <span className="flex-1" />
              <Button type="button" variant="outline" onClick={() => setRawOpen(true)}>
                <FileJson />
                {t('settings.rawEdit')}
              </Button>
              <Button
                type="button"
                variant="ghost"
                onClick={() => setResetting(true)}
                className="text-danger hover:bg-danger/10"
              >
                <RotateCcw />
                {t('settings.reset')}
              </Button>
            </div>

            <p className="flex flex-wrap items-center gap-1.5 text-xs text-muted">
              <FileJson className="size-3.5" />
              {t('settings.syncsTo', { path: data.path })}
            </p>

            <RawConfig open={rawOpen} path={data.path} onClose={() => setRawOpen(false)} />

            <ConfirmDialog
              open={resetting}
              destructive
              title={t('settings.reset')}
              message={t('settings.resetConfirm')}
              confirmLabel={t('settings.reset')}
              onCancel={() => setResetting(false)}
              onConfirm={() => {
                setResetting(false);
                reset.mutate();
              }}
            />
          </form>
        ) : null}
      </QueryState>
    </AppLayout>
  );
}

/** Only user edits are sent; anything untouched keeps the stored value. */
function toDraft(): Draft {
  return {};
}

/** Read-only token display plus a write-only field. */
function SecretField({
  label,
  stored,
  value,
  onChange,
}: {
  label: string;
  stored: ConfigResponse['github_token'];
  value: string;
  onChange: (next: string) => void;
}) {
  const { t } = useTranslation();
  return (
    <Field
      label={label}
      hint={
        <span className="flex items-center gap-2">
          {stored.set ? (
            <>
              <Badge tone="ok" className="font-normal">
                {t('settings.set')}
              </Badge>
              <span className="font-mono">···{stored.masked}</span>
            </>
          ) : (
            <Badge tone="neutral" className="font-normal">
              {t('settings.unset')}
            </Badge>
          )}
        </span>
      }
    >
      <div className="flex gap-1">
        <Input
          type="password"
          value={value === '__clear__' ? '' : value}
          onChange={(event) => onChange(event.target.value)}
          placeholder={stored.set ? t('llm.apiKeyKeep') : ''}
          autoComplete="off"
          spellCheck={false}
        />
        {stored.set ? (
          <Button
            variant="outline"
            size="icon"
            onClick={() => onChange(value === '__clear__' ? '' : '__clear__')}
            aria-label={t('settings.clear')}
            title={t('settings.clear')}
            className={value === '__clear__' ? 'border-danger text-danger' : undefined}
          >
            ×
          </Button>
        ) : null}
      </div>
    </Field>
  );
}

const SECTION_FLAGS = [
  { key: 'show_prs', labelKey: 'report.pullRequests' },
  { key: 'show_commits', labelKey: 'report.commits' },
  { key: 'show_issues', labelKey: 'report.issues' },
  { key: 'show_releases', labelKey: 'report.releases' },
  { key: 'show_contributors', labelKey: 'report.contributors' },
  { key: 'show_languages', labelKey: 'report.languages' },
  { key: 'show_top_labels', labelKey: 'report.labels' },
  { key: 'show_summary', labelKey: 'report.summary' },
] as const;

function SectionToggles({
  settings,
  draft,
  set,
}: {
  settings: ConfigResponse['export'] | undefined;
  draft: Draft;
  set: (key: string, value: string | number | boolean) => void;
}) {
  const { t } = useTranslation();
  return (
    <div className="grid gap-2 sm:grid-cols-2 lg:grid-cols-4">
      {SECTION_FLAGS.map((flag) => {
        const on = (draft[`export.${flag.key}`] ?? settings?.[flag.key] ?? true) as boolean;
        return (
          <label
            key={flag.key}
            className={cn(
              'flex cursor-pointer items-center gap-2 rounded-field border px-3 py-2 text-sm transition-colors',
              on
                ? 'border-accent-soft bg-accent-soft/50 text-ink'
                : 'border-line text-muted hover:bg-raised',
            )}
          >
            <input
              type="checkbox"
              checked={on}
              onChange={(event) => set(`export.${flag.key}`, event.target.checked)}
              className="size-4 accent-[var(--color-accent)]"
            />
            {t(flag.labelKey)}
          </label>
        );
      })}
    </div>
  );
}

/**
 * The layout and color settings. Every field here only affects a downloaded
 * file -- the console renders its own report -- so the card is scoped and
 * labelled accordingly instead of pretending to change what is on screen.
 */
function AdvancedExport({
  settings,
  draft,
  set,
  options,
}: {
  settings: ConfigResponse['export'] | undefined;
  draft: Draft;
  set: (key: string, value: string | number | boolean) => void;
  options: Awaited<ReturnType<typeof api.options>> | undefined;
}) {
  const { t } = useTranslation();
  const [open, setOpen] = useState(false);

  const num = (key: string, fallback: number, min: number, max: number, step = 1) => (
    <Field key={key} label={key.split('.').pop() ?? key} className="min-w-32">
      <Input
        type="number"
        min={min}
        max={max}
        step={step}
        value={Number(draft[key] ?? fallback)}
        onChange={(event) => set(key, Number(event.target.value))}
      />
    </Field>
  );

  const colorsFor = (section: 'pdf' | 'html') => {
    const keys =
      section === 'pdf'
        ? ['title_color', 'header_color', 'label_color', 'link_color', 'open_color', 'closed_color', 'merged_color']
        : ['title_color', 'header_color', 'label_color', 'link_color', 'open_color', 'closed_color', 'merged_color'];
    return keys.map((key) => (
      <ColorField
        key={`${section}.${key}`}
        label={`${t('settings.colorValue')} · ${key.replace('_color', '')}`}
        value={String(draft[`export.${section}.${key}`] ?? (settings as never)?.[section]?.[key] ?? '')}
        onChange={(next) => set(`export.${section}.${key}`, next)}
        names={options?.named_colors ?? []}
      />
    ));
  };

  return (
    <Card>
      <CardHeader>
        <div>
          <CardTitle>
            {t('settings.export')} · {t('settings.advancedExport')}
          </CardTitle>
          <p className="text-xs text-muted">{t('settings.downloadOnly')}</p>
        </div>
        <Button variant="ghost" size="sm" onClick={() => setOpen((prev) => !prev)}>
          {open ? t('app.close') : t('app.apply')}
        </Button>
      </CardHeader>
      {open ? (
        <CardContent className="space-y-5">
          <Group title={t('settings.md')} scope={t('settings.scope_web')}>
            {num('export.md.table_width', settings?.md.table_width ?? 60, 1, 60)}
          </Group>

          <Group title={t('settings.txt')} scope={t('settings.scope_web')}>
            {num('export.txt.table_width', settings?.txt.table_width ?? 56, 1, 56)}
          </Group>

          <Group title={t('settings.html')} scope={t('settings.scope_download')}>
            {colorsFor('html')}
          </Group>

          <Group title={t('settings.pdf')} scope={t('settings.scope_download')}>
            <div className="flex flex-wrap gap-4">
              {num('export.pdf.margin', settings?.pdf.margin ?? 50, 1, 50)}
              {num('export.pdf.line_height', settings?.pdf.line_height ?? 1.4, 1, 1.4, 0.05)}
              {num('export.pdf.col2_padding', settings?.pdf.col2_padding ?? 12, 2, 12)}
            </div>
            <div className="mt-3 flex flex-wrap gap-3">{colorsFor('pdf')}</div>
          </Group>

          <Group title={t('settings.xlsx')} scope={t('settings.scope_download')}>
            <div className="flex flex-wrap gap-4">
              {num('export.xlsx.font_size', settings?.xlsx.font_size ?? 11, 1, 11)}
              {num('export.xlsx.col_a_width', settings?.xlsx.col_a_width ?? 20, 1, 20, 0.5)}
              {num('export.xlsx.col_b_width', settings?.xlsx.col_b_width ?? 60, 1, 60, 0.5)}
            </div>
            <div className="mt-3 flex flex-wrap gap-4">
              <Field label="font_name" className="min-w-40">
                <Input
                  value={String(
                    draft['export.xlsx.font_name'] ?? settings?.xlsx.font_name ?? 'Calibri',
                  )}
                  onChange={(event) => set('export.xlsx.font_name', event.target.value)}
                />
              </Field>
              <Field label="sheet_name" className="min-w-40">
                <Input
                  value={String(
                    draft['export.xlsx.sheet_name'] ?? settings?.xlsx.sheet_name ?? 'Report',
                  )}
                  onChange={(event) => set('export.xlsx.sheet_name', event.target.value)}
                />
              </Field>
              <label className="flex items-center gap-2 self-end pb-2 text-sm text-ink-soft">
                <input
                  type="checkbox"
                  className="size-4 accent-[var(--color-accent)]"
                  checked={Boolean(
                    draft['export.xlsx.border'] ?? settings?.xlsx.border ?? true,
                  )}
                  onChange={(event) => set('export.xlsx.border', event.target.checked)}
                />
                border
              </label>
            </div>
          </Group>
        </CardContent>
      ) : null}
    </Card>
  );
}

function Group({
  title,
  scope,
  children,
}: {
  title: string;
  scope: string;
  children: React.ReactNode;
}) {
  return (
    <section className="space-y-3">
      <div className="flex items-center gap-2">
        <h3 className="text-xs font-semibold tracking-wide text-ink uppercase">{title}</h3>
        <Badge tone="neutral" className="font-normal">
          {scope}
        </Badge>
      </div>
      <div className="flex flex-wrap items-end gap-4">{children}</div>
    </section>
  );
}

function RawConfig({
  open,
  path,
  onClose,
}: {
  open: boolean;
  path: string;
  onClose: () => void;
}) {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const [text, setText] = useState('');
  const [invalid, setInvalid] = useState(false);

  const raw = useQuery({
    queryKey: ['config-raw'],
    queryFn: api.getRawConfig,
    enabled: open,
  });

  useEffect(() => {
    if (raw.data !== undefined) {
      setText(raw.data);
      setInvalid(false);
    }
  }, [raw.data]);

  const save = useMutation({
    mutationFn: api.putRawConfig,
    onSuccess: () => {
      toast.success(t('settings.rawSaved'));
      void queryClient.invalidateQueries({ queryKey: ['config'] });
      onClose();
    },
    onError: (cause) =>
      toast.error(cause instanceof Error ? cause.message : t('errors.generic')),
  });

  const submit = () => {
    try {
      const parsed: unknown = JSON.parse(text);
      if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
        setInvalid(true);
        return;
      }
    } catch {
      setInvalid(true);
      return;
    }
    setInvalid(false);
    save.mutate(text);
  };

  return (
    <Modal
      open={open}
      onClose={onClose}
      title={t('settings.raw')}
      description={t('settings.rawHint', { path })}
      className="w-[min(94vw,46rem)]"
      footer={
        <>
          <Button variant="ghost" size="sm" onClick={onClose}>
            {t('app.cancel')}
          </Button>
          <Button
            variant="primary"
            size="sm"
            disabled={save.isPending || invalid || raw.isPending || raw.isError}
            onClick={submit}
          >
            {t('settings.rawSave')}
          </Button>
        </>
      }
    >
      <CodeTextarea
        rows={20}
        value={text}
        lang="json"
        spellCheck={false}
        invalid={invalid}
        onChange={(value) => {
          setText(value);
          setInvalid(false);
        }}
      />
      {invalid ? <p className="mt-2 text-xs text-danger">{t('settings.rawInvalid')}</p> : null}
      {raw.isError ? (
        <InlineError
          message={raw.error instanceof Error ? raw.error.message : t('errors.generic')}
        />
      ) : null}
      {save.isError ? (
        <InlineError
          message={save.error instanceof Error ? save.error.message : t('errors.generic')}
        />
      ) : null}
    </Modal>
  );
}
