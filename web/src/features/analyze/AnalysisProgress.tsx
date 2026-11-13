import { CheckCircle2, CircleDashed, Loader2, XCircle } from 'lucide-react';
import { useEffect, useMemo, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';

import { Badge } from '@/components/ui/badge';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import type { ProgressEvent } from '@/hooks/useJob';
import { cn } from '@/lib/cn';
import { useStageLabel } from '@/lib/stages';

import { buildProgressModel, rowFraction, type ProgressRow } from '@/features/analyze/progress';

const STATE_ICONS = {
  start: Loader2,
  update: CircleDashed,
  ok: CheckCircle2,
  fail: XCircle,
} as const;

type Phase = 'submitting' | 'running' | 'done' | 'failed' | 'cancelled';

function clamp(value: number): number {
  return Math.max(0, Math.min(1, value));
}

function formatElapsed(ms: number): string {
  const total = Math.floor(ms / 1000);
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${minutes}:${String(seconds).padStart(2, '0')}`;
}

/** A live timer makes an otherwise silent LLM call visibly still running. */
function useElapsed(phase: Phase): number {
  const [now, setNow] = useState(() => Date.now());
  const startedAt = useRef<number | null>(null);

  useEffect(() => {
    if (phase !== 'submitting' && phase !== 'running') {
      startedAt.current = null;
      return;
    }
    if (startedAt.current === null) startedAt.current = Date.now();
    setNow(Date.now());
    const timer = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, [phase]);

  return startedAt.current === null ? 0 : Math.max(0, now - startedAt.current);
}

/**
 * The overall meter: solid for work actually finished, a travelling sheen for
 * the part that is still running. An indeterminate stage keeps the solid part
 * intact instead of replacing the whole bar with a pulsing third.
 */
function StageMeter({
  value,
  running,
  tone,
  label,
}: {
  value: number | null;
  running: boolean;
  tone: 'accent' | 'ok' | 'danger' | 'neutral';
  label?: string;
}) {
  const pct = value === null ? null : clamp(value);
  const toneClass =
    tone === 'ok' ? 'bg-ok' : tone === 'danger' ? 'bg-danger' : tone === 'neutral' ? 'bg-muted' : 'bg-accent';

  return (
    <div
      role="progressbar"
      aria-valuenow={pct === null ? undefined : Math.round(pct * 100)}
      aria-valuemin={0}
      aria-valuemax={100}
      aria-label={label}
      className="relative h-2 w-full overflow-hidden rounded-full bg-raised"
    >
      {pct === null ? (
        <div className={cn('h-full w-1/3 rounded-full', toneClass, running && 'animate-pulse')} />
      ) : (
        <>
          <div
            className={cn(
              'h-full rounded-full transition-[width] duration-500 ease-out',
              toneClass,
            )}
            style={{ width: `${pct * 100}%` }}
          />
          {running && pct < 1 ? (
            <div
              className="absolute inset-y-0 overflow-hidden"
              style={{ left: `${pct * 100}%`, right: 0 }}
              aria-hidden
            >
              <div className="animate-bar-sheen h-full w-1/2" />
            </div>
          ) : null}
        </>
      )}
    </div>
  );
}

/**
 * Live stage log.
 *
 * The server emits one event per animation tick, which for a fetch phase is
 * every completed resource. Stages interleave -- a long fetch keeps ticking
 * while a slow LLM call is already running -- so events are grouped per stage
 * rather than listed in arrival order: without that the same stage appears as a
 * dozen rows separated by others, and a fetch reads as eight near-identical
 * lines instead of one row counting up.
 *
 * The summary above the log is driven by the same fold, but counts completed
 * phases and the determinate part of the current one. A spinner therefore no
 * longer erases the fetch progress that preceded it, and two repositories or
 * two compare passes divide the bar rather than each claiming 100%.
 *
 * The list auto-scrolls while the user has not scrolled up, because watching a
 * seven-step fetch is the entire feedback loop for a slow analysis.
 */
export function AnalysisProgress({
  progress,
  phase,
  onCancel,
  totalRepos,
  jobKind,
}: {
  progress: ProgressEvent[];
  phase: Phase;
  onCancel?: () => void;
  /** Known repository count for a batch/subscription run. */
  totalRepos?: number;
  /** Compare repeats the fetch/analyze pair, so it has two passes. */
  jobKind?: 'analyze' | 'compare' | 'subscriptions';
}) {
  const { t } = useTranslation();
  const translate = useStageLabel();
  const listRef = useRef<HTMLOListElement>(null);
  const pinnedRef = useRef(true);
  const highWaterRef = useRef(0);

  const live = phase === 'submitting' || phase === 'running';
  const model = useMemo(
    () => buildProgressModel(progress, translate, { totalRepos, jobKind }),
    [progress, translate, totalRepos, jobKind],
  );
  const elapsed = useElapsed(phase);

  // The denominator can grow when a not-yet-known repository or optional stage
  // appears. Keep the best figure so far rather than making the bar jump back.
  if (model.overall === null) highWaterRef.current = 0;
  else highWaterRef.current = Math.max(highWaterRef.current, model.overall);
  const overall =
    phase === 'done' ? 1 : model.overall === null ? null : highWaterRef.current;

  const current = live ? (model.active ?? model.latest) : (model.latest ?? model.rows.at(-1) ?? null);
  const currentLabel = current?.stage ?? null;
  const lastRow = model.rows.at(-1);
  const lastTicks = lastRow?.ticks;

  useEffect(() => {
    const list = listRef.current;
    if (!list || !pinnedRef.current) return;
    list.scrollTop = list.scrollHeight;
  }, [model.rows.length, lastTicks]);

  const onScroll = () => {
    const list = listRef.current;
    if (!list) return;
    pinnedRef.current = list.scrollHeight - list.scrollTop - list.clientHeight < 24;
  };

  const detail = (() => {
    if (!live) {
      if (phase === 'done') return t('state.done');
      if (phase === 'failed') return t('state.failed');
      if (phase === 'cancelled') return t('state.cancelled');
      return t('analyze.waiting');
    }
    if (current && current.total > 0) return `${current.current} / ${current.total}`;
    return t('analyze.working');
  })();

  const repoDetail =
    live && model.totalRepos && model.totalRepos > 1
      ? t('analyze.repoProgress', { done: model.reposDone, total: model.totalRepos })
      : null;

  const summaryIcon =
    phase === 'done'
      ? CheckCircle2
      : phase === 'failed'
        ? XCircle
        : phase === 'cancelled'
          ? CircleDashed
          : live
            ? Loader2
            : CircleDashed;

  const SummaryIcon = summaryIcon;
  const meterTone =
    phase === 'done'
      ? 'ok'
      : phase === 'failed'
        ? 'danger'
        : phase === 'cancelled'
          ? 'neutral'
          : 'accent';

  return (
    <Card>
      <CardHeader>
        <div className="flex items-center gap-2">
          <CardTitle>{t('analyze.progress')}</CardTitle>
          {live ? (
            <Badge tone="accent">{t(`state.${phase === 'running' ? 'running' : 'queued'}`)}</Badge>
          ) : (
            <Badge tone={phase === 'done' ? 'ok' : phase === 'failed' ? 'danger' : 'neutral'}>
              {t(`state.${phase}`)}
            </Badge>
          )}
        </div>
        {onCancel && live ? (
          <button
            type="button"
            onClick={onCancel}
            className="text-xs font-medium text-accent hover:underline"
          >
            {t('analyze.cancelJob')}
          </button>
        ) : null}
      </CardHeader>
      <CardContent className="space-y-3">
        <div className="space-y-2">
          <div className="flex items-start gap-2.5">
            <SummaryIcon
              className={cn(
                'mt-0.5 size-4 shrink-0',
                live && 'animate-spin text-accent',
                phase === 'done' && 'text-ok',
                phase === 'failed' && 'text-danger',
                (phase === 'cancelled' || (!live && phase !== 'done' && phase !== 'failed')) &&
                  'text-muted',
              )}
            />
            <div className="min-w-0 flex-1" aria-live="polite">
              <p className="truncate text-sm font-medium text-ink">
                {currentLabel ?? t('analyze.waiting')}
              </p>
              <p className="mt-0.5 flex flex-wrap items-center gap-x-2 gap-y-0.5 text-xs text-muted">
                <span className="nums">{detail}</span>
                {repoDetail ? (
                  <>
                    <span aria-hidden>·</span>
                    <span className="nums">{repoDetail}</span>
                  </>
                ) : null}
              </p>
            </div>
            <div className="shrink-0 text-right">
              {overall !== null ? (
                <p className="nums text-lg font-semibold leading-none text-ink">
                  {Math.round(overall * 100)}%
                </p>
              ) : null}
              {live && elapsed > 0 ? (
                <p className="nums mt-1 text-[11px] text-muted">
                  {t('analyze.elapsed', { time: formatElapsed(elapsed) })}
                </p>
              ) : null}
            </div>
          </div>

          <StageMeter
            value={overall}
            running={live}
            tone={meterTone}
            label={currentLabel ?? undefined}
          />
        </div>

        {model.rows.length === 0 ? (
          <p className="text-xs text-muted">{t('analyze.waiting')}</p>
        ) : (
          <ol
            ref={listRef}
            onScroll={onScroll}
            className="max-h-64 space-y-1 overflow-y-auto scrollbar-thin"
          >
            {model.rows.map((row) => (
              <StageRow key={row.key} row={row} live={live} />
            ))}
          </ol>
        )}
      </CardContent>
    </Card>
  );
}

function StageRow({ row, live }: { row: ProgressRow; live: boolean }) {
  const rowLive = live && (row.tone === 'start' || row.tone === 'update');
  const Icon = rowLive ? Loader2 : STATE_ICONS[row.tone];
  const fraction = rowFraction(row);

  return (
    <li
      className={cn(
        'animate-enter flex items-start gap-2 rounded px-1.5 py-1 text-xs transition-colors',
        rowLive && 'bg-accent-soft/50',
      )}
    >
      <Icon
        className={cn(
          'mt-0.5 size-3.5 shrink-0',
          rowLive && 'animate-spin text-accent',
          !rowLive && row.tone === 'ok' && 'text-ok',
          !rowLive && row.tone === 'fail' && 'text-danger',
          !rowLive && (row.tone === 'start' || row.tone === 'update') && 'text-muted',
        )}
      />
      <div className="min-w-0 flex-1">
        {row.showRepo && row.repo ? (
          <span className="block truncate text-[11px] text-muted" title={row.repo}>
            {row.repo}
          </span>
        ) : null}
        <div className="flex items-baseline justify-between gap-2">
          <span
            className={cn(
              'min-w-0 break-words',
              row.tone === 'ok' ? 'text-muted' : 'text-ink-soft',
              rowLive && 'font-medium text-ink',
            )}
          >
            {row.stage}
          </span>
          <span className="flex shrink-0 items-baseline gap-1.5">
            {row.total > 0 ? (
              <span className="nums text-muted">
                {row.current}/{row.total}
              </span>
            ) : null}
            {row.ticks > 1 ? <span className="nums text-muted">x{row.ticks}</span> : null}
          </span>
        </div>
        {fraction !== null && row.tone !== 'ok' ? (
          <div className="mt-1 h-0.5 overflow-hidden rounded-full bg-raised" aria-hidden>
            <div
              className={cn(
                'h-full rounded-full transition-[width] duration-300',
                rowLive ? 'bg-accent' : 'bg-accent/60',
              )}
              style={{ width: `${fraction * 100}%` }}
            />
          </div>
        ) : null}
      </div>
    </li>
  );
}
