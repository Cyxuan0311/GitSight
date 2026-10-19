import { useQuery, useQueryClient } from '@tanstack/react-query';
import { Check, Copy, Download } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';

import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Tooltip } from '@/components/ui/tooltip';
import { api } from '@/lib/api';
import { cn } from '@/lib/cn';
import { splitLabels } from '@/lib/format';
import type { MetaOptions } from '@/lib/types';

/** Copy-to-clipboard with a transient confirmation, reused across pages. */
export function CopyButton({ text, label }: { text: string; label?: string }) {
  const { t } = useTranslation();
  const [copied, setCopied] = useState(false);

  const copy = async () => {
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      // Clipboard access needs a secure context; fall back to a selection.
      const area = document.createElement('textarea');
      area.value = text;
      document.body.appendChild(area);
      area.select();
      document.execCommand('copy');
      area.remove();
    }
    setCopied(true);
    setTimeout(() => setCopied(false), 1600);
  };

  return (
    <Tooltip content={copied ? t('app.copied') : (label ?? t('app.copy'))}>
      <Button variant="ghost" size="icon-sm" onClick={() => void copy()} aria-label={t('app.copy')}>
        {copied ? <Check className="text-ok" /> : <Copy />}
      </Button>
    </Tooltip>
  );
}

/**
 * Format download strip. Each button asks the server to re-render the stored
 * analysis in the chosen format, so switching format never refetches.
 */
export function ExportBar({
  jobId,
  repo,
  enabled,
}: {
  jobId: string;
  /** Required when the job produced several reports. */
  repo?: string;
  enabled: boolean;
}) {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const { data: options } = useQuery({ queryKey: ['meta-options'], queryFn: api.options });
  const [busy, setBusy] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const run = async (format: string) => {
    setBusy(format);
    setError(null);
    try {
      await api.downloadExportFor(jobId, format, repo);
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : t('errors.generic'));
    } finally {
      setBusy(null);
      void queryClient.invalidateQueries({ queryKey: ['quota'] });
    }
  };

  if (!enabled) {
    return (
      <p className="text-xs text-muted">{t('report.exportPending')}</p>
    );
  }

  return (
    <div className="space-y-2">
      <div className="flex flex-wrap items-center gap-1.5">
        {(options?.formats ?? []).map((format) => (
          <Button
            key={format.id}
            variant="outline"
            size="sm"
            disabled={busy !== null}
            onClick={() => void run(format.id)}
            title={format.exports_all_items ? t('report.exportsAll') : undefined}
          >
            <Download />
            {format.id.toUpperCase()}
          </Button>
        ))}
        <span className="ml-1 text-xs text-muted">{t('report.exportHint')}</span>
      </div>
      {error ? <p className="text-xs text-danger">{error}</p> : null}
    </div>
  );
}

/** Renders the comma-joined label string as individual chips. */
export function LabelChips({
  labels,
  limit = 8,
  className,
}: {
  labels: string | undefined;
  limit?: number;
  className?: string;
}) {
  const list = splitLabels(labels);
  if (list.length === 0) return <span className="text-xs text-muted">-</span>;

  const shown = list.slice(0, limit);
  const rest = list.length - shown.length;

  return (
    <span className={cn('flex flex-wrap items-center gap-1', className)}>
      {shown.map((label) => (
        <Badge key={label} tone="neutral" className="font-normal">
          {label}
        </Badge>
      ))}
      {rest > 0 ? (
        <Tooltip content={list.slice(limit).join(', ')}>
          <Badge tone="neutral" className="font-normal">
            +{rest}
          </Badge>
        </Tooltip>
      ) : null}
    </span>
  );
}

/** Empty/zero-aware stat tile used throughout the report and dashboards. */
export function StatCard({
  label,
  value,
  hint,
  tone,
  icon,
}: {
  label: string;
  value: React.ReactNode;
  hint?: React.ReactNode;
  tone?: 'default' | 'open' | 'closed' | 'merged' | 'accent';
  icon?: React.ReactNode;
}) {
  const toneClass =
    tone === 'open'
      ? 'text-open'
      : tone === 'closed'
        ? 'text-closed'
        : tone === 'merged'
          ? 'text-merged'
          : tone === 'accent'
            ? 'text-accent'
            : 'text-ink';

  return (
    <div className="rounded-card border border-line bg-surface p-4">
      <div className="flex items-start justify-between gap-2">
        <p className="text-xs text-muted">{label}</p>
        {icon ? <span className="text-muted [&_svg]:size-4">{icon}</span> : null}
      </div>
      <p className={cn('nums mt-1.5 text-2xl leading-none font-semibold', toneClass)}>{value}</p>
      {hint ? <p className="mt-1.5 text-xs text-muted">{hint}</p> : null}
    </div>
  );
}

/** Formats are a global preference rather than per-page, so it loads once. */
export function useMetaOptions() {
  return useQuery<MetaOptions>({ queryKey: ['meta-options'], queryFn: api.options });
}