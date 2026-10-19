import { useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';

import { ApiError, api } from '@/lib/api';
import type { Job, JobResult } from '@/lib/types';

export interface ProgressEvent {
  stage: string;
  current: number;
  total: number;
  state: number;
}

export type JobPhase = 'idle' | 'submitting' | 'running' | 'done' | 'failed' | 'cancelled';

interface JobRun {
  id: string;
  phase: JobPhase;
  progress: ProgressEvent[];
  result: JobResult | null;
  error: string | null;
}

/**
 * Drives one analysis job end to end.
 *
 * The server streams progress over SSE and holds the finished analysis in
 * memory, so this hook owns three things: posting the job, tailing the stream,
 * and fetching the result once the terminal event arrives. It reconnects on
 * error and re-reads the job record, because a progress stream that drops
 * should not lose the analysis.
 *
 * One hook instance owns one run. A page that renders progress and results must
 * therefore take the instance it was given rather than calling this again for
 * itself: a second instance has its own state and would never see the run.
 *
 * `initialId` adopts a job that is already in flight, which is what lets a
 * reloaded or shared `/analyze?job=...` link pick the run back up.
 */
export function useJob(initialId?: string) {
  const { t } = useTranslation();
  const [run, setRun] = useState<JobRun>({ id: '', phase: 'idle', progress: [], result: null, error: null });

  const sourceRef = useRef<EventSource | null>(null);
  const jobRef = useRef<string>('');
  // Guards against a late stream from a previous job updating the current one.
  const generationRef = useRef(0);
  // The job handed in by the route, adopted exactly once.
  const adoptedRef = useRef('');

  const closeStream = useCallback(() => {
    sourceRef.current?.close();
    sourceRef.current = null;
  }, []);

  const fetchResult = useCallback(async (id: string) => {
    const result = await api.getResult(id);
    setRun((prev) => (prev.id === id ? { ...prev, result } : prev));
    return result;
  }, []);

  /** Opens the event stream for a job id. Exposed so a route can resume one. */
  const watch = useCallback(
    (id: string) => {
      closeStream();
      jobRef.current = id;
      const generation = ++generationRef.current;

      const finish = async (phase: JobPhase, error?: string) => {
        closeStream();
        if (generation !== generationRef.current) return;
        setRun((prev) => (prev.id === id ? { ...prev, phase, error: error ?? null } : prev));
        if (phase === 'done') {
          try {
            await fetchResult(id);
          } catch (cause) {
            setRun((prev) =>
              prev.id === id
                ? {
                    ...prev,
                    phase: 'failed',
                    error:
                      cause instanceof ApiError
                        ? cause.message
                        : `${t('errors.noResult')}`,
                  }
                : prev,
            );
          }
        }
      };

      // EventSource reconnects on its own; onerror only tells us it dropped, so
      // reconcile against the server rather than surfacing a false failure.
      const source = new EventSource(`/api/jobs/${id}/events`);
      sourceRef.current = source;

      const onProgress = (raw: MessageEvent) => {
        if (generation !== generationRef.current) return;
        try {
          const data = JSON.parse(raw.data) as ProgressEvent;
          setRun((prev) =>
            prev.id === id
              ? { ...prev, phase: 'running', progress: [...prev.progress, data] }
              : prev,
          );
        } catch {
          /* A malformed frame should not tear down the stream. */
        }
      };

      source.addEventListener('hello', () => {
        if (generation === generationRef.current)
          setRun((prev) => (prev.id === id && prev.phase !== 'done' ? { ...prev, phase: 'running' } : prev));
      });
      source.addEventListener('progress', onProgress as EventListener);
      source.addEventListener('stage-done', onProgress as EventListener);
      source.addEventListener('stage-failed', onProgress as EventListener);

      source.addEventListener('done', () => void finish('done'));
      source.addEventListener('error', () => void finish('failed'));
      source.addEventListener('cancelled', () => void finish('cancelled'));

      source.onerror = () => {
        if (generation !== generationRef.current) return;
        void api
          .getJob(id)
          .then((job: Job) => {
            if (job.state === 'done') return finish('done');
            if (job.state === 'failed') return finish('failed', job.error);
            if (job.state === 'cancelled') return finish('cancelled');
            // Still running server-side: let EventSource retry on its own.
          })
          .catch((cause: unknown) =>
            finish('failed', cause instanceof Error ? cause.message : t('errors.generic')),
          );
      };
    },
    [closeStream, fetchResult, t],
  );

  const start = useCallback(
    async (submit: () => Promise<Job>) => {
      closeStream();
      const generation = ++generationRef.current;
      setRun({ id: '', phase: 'submitting', progress: [], result: null, error: null });
      try {
        const job = await submit();
        if (generation !== generationRef.current) return null;
        setRun({ id: job.id, phase: 'running', progress: [], result: null, error: null });
        watch(job.id);
        return job;
      } catch (cause) {
        if (generation !== generationRef.current) return null;
        setRun({
          id: '',
          phase: 'failed',
          progress: [],
          result: null,
          error: cause instanceof Error ? cause.message : t('errors.generic'),
        });
        return null;
      }
    },
    [closeStream, watch, t],
  );

  const cancel = useCallback(async () => {
    const id = jobRef.current;
    if (!id) return;
    try {
      await api.cancelJob(id);
    } catch {
      /* The job may have finished between the click and the request; the
         terminal stream event still resolves the UI either way. */
    }
  }, []);

  const reset = useCallback(() => {
    closeStream();
    generationRef.current++;
    jobRef.current = '';
    adoptedRef.current = '';
    setRun({ id: '', phase: 'idle', progress: [], result: null, error: null });
  }, [closeStream]);

  // Adopting a routed job id is a one-time transition; the resume effect below
  // opens the stream as soon as run.id is set.
  useEffect(() => {
    const id = initialId?.trim();
    if (!id || adoptedRef.current === id) return;
    adoptedRef.current = id;
    jobRef.current = id;
    setRun({ id, phase: 'running', progress: [], result: null, error: null });
  }, [initialId]);

  // A job id arriving from the URL is watched once on mount, which is what
  // makes a reloaded analysis page resume instead of restart.
  useEffect(() => {
    if (run.id && run.phase === 'running' && !sourceRef.current) watch(run.id);
  }, [run.id, run.phase, watch]);

  useEffect(() => closeStream, [closeStream]);

  return { ...run, start, watch, cancel, reset };
}

/** The handle a page passes to whatever component submits on its behalf. */
export type JobRunner = ReturnType<typeof useJob>;

/** Fraction of the current determinate stage, or null when indeterminate. */
export function stageFraction(event: ProgressEvent | undefined): number | null {
  if (!event || event.total <= 0) return null;
  return Math.max(0, Math.min(1, event.current / event.total));
}

/** Label for the most recent progress event. */
export function latestStage(progress: ProgressEvent[]): ProgressEvent | undefined {
  return progress.length > 0 ? progress[progress.length - 1] : undefined;
}