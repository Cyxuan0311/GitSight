import { useTranslation } from 'react-i18next';

import { Field, Input, Select, Textarea } from '@/components/ui/field';
import type { MetaOptions } from '@/lib/types';

import type { RunOptionValues } from './runOptions';

export function ExportOverrides({
  value,
  onChange,
  error,
  keys,
}: {
  value: string;
  onChange: (next: string) => void;
  error?: string;
  keys?: string[];
}) {
  const { t } = useTranslation();

  // Clicking a key appends "key=" so only the value is left to type.
  const insert = (key: string) => {
    const prefix = value.length === 0 || value.endsWith('\n') ? '' : '\n';
    onChange(`${value}${prefix}${key}=`);
  };

  return (
    <div className="space-y-2">
      <Field
        label={t('analyze.exportOverrides')}
        hint={t('analyze.exportOverridesHint')}
        error={error}
      >
        <Textarea
          rows={2}
          spellCheck={false}
          className="font-mono text-xs"
          value={value}
          onChange={(event) => onChange(event.target.value)}
          invalid={Boolean(error)}
        />
      </Field>

      {keys && keys.length > 0 ? (
        <div className="flex flex-wrap gap-1">
          {keys.map((key) => (
            <button
              key={key}
              type="button"
              onClick={() => insert(key)}
              className="rounded-field border border-line px-1.5 py-0.5 font-mono text-[11px] text-muted transition-colors hover:border-accent hover:text-accent"
            >
              {key}
            </button>
          ))}
        </div>
      ) : null}
    </div>
  );
}

export function AdvancedRunFields({
  value,
  onChange,
  options,
  exportError,
}: {
  value: RunOptionValues;
  onChange: (next: RunOptionValues) => void;
  options?: MetaOptions;
  exportError?: string;
}) {
  const { t } = useTranslation();
  const set = <K extends keyof RunOptionValues>(key: K, next: RunOptionValues[K]) =>
    onChange({ ...value, [key]: next });

  return (
    <div className="space-y-4 rounded-card border border-line bg-raised/50 p-4">
      <div className="grid gap-4 sm:grid-cols-2">
        <Field label={t('analyze.platform')} hint={t('subs.inherit')}>
          <Select value={value.platform} onChange={(event) => set('platform', event.target.value)}>
            <option value="">{t('subs.inherit')}</option>
            {(options?.platforms ?? []).map((platform) => (
              <option key={platform.id} value={platform.id}>
                {platform.id}
              </option>
            ))}
          </Select>
        </Field>

        <Field label={t('analyze.branch')} hint={t('subs.inherit')}>
          <Input
            placeholder="main"
            autoComplete="off"
            spellCheck={false}
            value={value.branch}
            onChange={(event) => set('branch', event.target.value)}
          />
        </Field>

        <Field label={t('analyze.lang')} hint={t('subs.inherit')}>
          <Select value={value.lang} onChange={(event) => set('lang', event.target.value)}>
            <option value="">{t('subs.inherit')}</option>
            {(options?.languages ?? []).map((language) => (
              <option key={language.id} value={language.id}>
                {language.label}
              </option>
            ))}
          </Select>
        </Field>

        <Field label={t('analyze.llmMode')} hint={t('subs.inherit')}>
          <Select value={value.llm_mode} onChange={(event) => set('llm_mode', event.target.value)}>
            <option value="">{t('subs.inherit')}</option>
            {(options?.llm_modes ?? []).map((mode) => (
              <option key={mode.id} value={mode.id}>
                {mode.id}
              </option>
            ))}
          </Select>
        </Field>

        <Field label={t('analyze.maxPages')} hint={t('subs.inherit')}>
          <Input
            type="number"
            min={1}
            value={value.max_pages}
            onChange={(event) => set('max_pages', event.target.value)}
          />
        </Field>

        <Field label={t('analyze.apiBase')} hint={t('subs.inherit')}>
          <Input
            autoComplete="off"
            spellCheck={false}
            value={value.api_base}
            onChange={(event) => set('api_base', event.target.value)}
          />
        </Field>

        <Field label={t('analyze.proxy')} hint={t('subs.inherit')}>
          <Input
            autoComplete="off"
            spellCheck={false}
            value={value.proxy}
            onChange={(event) => set('proxy', event.target.value)}
          />
        </Field>

        <Field label={t('analyze.token')} hint={t('analyze.tokenHint')}>
          <Input
            type="password"
            autoComplete="off"
            spellCheck={false}
            placeholder="ghp_..."
            value={value.token}
            onChange={(event) => set('token', event.target.value)}
          />
        </Field>
      </div>

      <Field label={t('analyze.filters')} hint={t('analyze.filtersHint')}>
        <Textarea
          rows={2}
          spellCheck={false}
          value={value.filters}
          onChange={(event) => set('filters', event.target.value)}
        />
      </Field>

      <ExportOverrides
        value={value.exportOverrides}
        onChange={(next) => set('exportOverrides', next)}
        error={exportError}
        keys={options?.export_keys}
      />

      <label className="flex items-center gap-2 text-sm text-ink-soft">
        <input
          type="checkbox"
          className="size-4 accent-[var(--color-accent)]"
          checked={value.no_cache}
          onChange={(event) => set('no_cache', event.target.checked)}
        />
        {t('analyze.noCache')}
      </label>
    </div>
  );
}
