import { CheckCircle2, CircleDashed, Loader2, XCircle } from 'lucide-react';
import { useEffect, useMemo, useRef } from 'react';
import { useTranslation } from 'react-i18next';

import { Badge } from '@/components/ui/badge';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Progress } from '@/components/ui/feedback';
import { latestStage, stageFraction, type ProgressEvent } from '@/hooks/useJob';
import { cn } from '@/lib/cn';
import { useStageLabel } from '@/lib/stages';

const STATE_ICONS = {
  start: Loader2,
  update: CircleDashed,
  ok: CheckCircle2,
  fail: XCircle,
} as const;

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
 * The list auto-scrolls while the user has not scrolled up, because watching a
 * seven-step fetch is the entire feedback loop for a slow analysis.
 */
export function AnalysisProgress({
  progress,
  phase,
  onCancel,
}: {
  progress: ProgressEvent[];
  phase: 'submitting' | 'running' | 'done' | 'failed' | 'cancelled';
  onCancel?: () => void;
}) {
  const { t } = useTranslation();
  const translate = useStageLabel();
  const listRef = useRef<HTMLOListElement>(null);
  const pinnedRef = useRef(true);

  const rows = useMemo(() => groupByStage(progress, translate), [progress, translate]);
  // Scrolling again on the last stage's tick count keeps the newest row in view
  // while the stages before it stay put.
  const lastTicks = rows[rows.length - 1]?.ticks;
  const current = latestStage(progress);
  const currentLabel = current ? translate(current.stage) : null;

  useEffect(() => {
    const list = listRef.current;
    if (!list || !pinnedRef.current) return;
    list.scrollTop = list.scrollHeight;
  }, [rows.length, lastTicks]);

  const onScroll = () => {
    const list = listRef.current;
    if (!list) return;
    pinnedRef.current = list.scrollHeight - list.scrollTop - list.clientHeight < 24;
  };

  return (
    <Card>
      <CardHeader>
        <div className="flex items-center gap-2">
          <CardTitle>{t('analyze.progress')}</CardTitle>
          {phase === 'submitting' || phase === 'running' ? (
            <Badge tone="accent">{t(`state.${phase === 'running' ? 'running' : 'queued'}`)}</Badge>
          ) : (
            <Badge tone={phase === 'done' ? 'ok' : phase === 'failed' ? 'danger' : 'neutral'}>
              {t(`state.${phase}`)}
            </Badge>
          )}
        </div>
        {onCancel && (phase === 'running' || phase === 'submitting') ? (
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
        <Progress
          value={stageFraction(current)}
          label={currentLabel ? currentLabel.repo || currentLabel.stage : undefined}
          className={phase === 'done' ? '[&>div]:bg-ok' : undefined}
        />

        {rows.length === 0 ? (
          <p className="text-xs text-muted">{t('analyze.waiting')}</p>
        ) : (
          <ol
            ref={listRef}
            onScroll={onScroll}
            className="max-h-64 space-y-1 overflow-y-auto scrollbar-thin"
          >
            {rows.map((row) => {
              const Icon = STATE_ICONS[row.tone];
              return (
                <li
                  key={row.key}
                  className="animate-enter flex items-start gap-2 rounded px-1 py-0.5 text-xs"
                >
                  <Icon
                    className={cn(
                      'mt-0.5 size-3.5 shrink-0',
                      row.tone === 'ok' && 'text-ok',
                      row.tone === 'fail' && 'text-danger',
                      row.tone === 'update' && 'text-muted',
                      row.tone === 'start' && 'animate-spin text-accent',
                    )}
                  />
                  <span className="min-w-0 flex-1">
                    {row.repo ? (
                      <span className="block truncate text-muted" title={row.repo}>
                        {row.repo}
                      </span>
                    ) : null}
                    <span className="block break-words text-ink-soft">{row.stage}</span>
                  </span>
                  {row.total > 0 ? (
                    <span className="nums shrink-0 text-muted">
                      {row.current}/{row.total}
                    </span>
                  ) : null}
                  {row.ticks > 1 ? (
                    <span className="nums shrink-0 text-muted">x{row.ticks}</span>
                  ) : null}
                </li>
              );
            })}
          </ol>
        )}
      </CardContent>
    </Card>
  );
}

interface Row {
  key: string;
  repo: string;
  stage: string;
  current: number;
  total: number;
  tone: 'start' | 'update' | 'ok' | 'fail';
  /** Update ticks after the one that opened the stage; the counter beside the row. */
  ticks: number;
}

type Tone = Row['tone'];

/** State 0=start 1=update 2=ok 3=fail, matching anim.h's ANIM_PROG_*. */
function toneOf(state: number): Tone {
  return state === 0 ? 'start' : state === 1 ? 'update' : state === 2 ? 'ok' : 'fail';
}

/** A terminal outcome outranks a transient state; the reverse never happens. */
const TONE_RANK: Record<Tone, number> = { start: 0, update: 1, ok: 2, fail: 2 };

/**
 * Folds the event stream into one row per stage, in the order the stages were
 * first seen. A repeated stage updates in place rather than appending, which is
 * what keeps an interleaved fetch from scattering across the list.
 */
function groupByStage(
  events: ProgressEvent[],
  translate: (raw: string) => { repo: string; stage: string },
): Row[] {
  const rows: Row[] = [];
  const index = new Map<string, Row>();

  for (const event of events) {
    const { repo, stage } = translate(event.stage ?? '');
    const key = `${repo}|${stage}`;
    const tone = toneOf(event.state);

    const existing = index.get(key);
    if (existing) {
      existing.current = event.current;
      existing.total = event.total;
      existing.ticks += 1;
      if (TONE_RANK[tone] > TONE_RANK[existing.tone]) existing.tone = tone;
      continue;
    }

    const row: Row = {
      key,
      repo,
      stage,
      current: event.current,
      total: event.total,
      tone,
      // The event that opens the stage is not a repeat, so a seven-step fetch
      // reads "7/7 x7" rather than counting its own opening tick.
      ticks: 0,
    };
    index.set(key, row);
    rows.push(row);
  }

  return rows;
}