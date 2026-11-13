import type { ProgressEvent } from '@/hooks/useJob';
import { stageSpec, type StageKind, type StageSpec } from '@/lib/stages';

export type Tone = 'start' | 'update' | 'ok' | 'fail';

/** One folded stage: every tick of the same stage updates this row in place. */
export interface ProgressRow {
  key: string;
  repo: string;
  stage: string;
  kind: StageKind;
  order: number;
  weight: number;
  current: number;
  total: number;
  tone: Tone;
  /** Updates after the event that opened the row; rendered as "xN". */
  ticks: number;
  /** Which pass this row belongs to: compare runs the same stages twice. */
  round: number;
  /** The first row for a repository, so its URL is printed once. */
  showRepo: boolean;
  /** Event ordering inside this row, used to find the most recent live stage. */
  lastEvent: number;
}

export interface ProgressOptions {
  /** How many repositories the job will analyze, when the page knows it. */
  totalRepos?: number;
  /** Compare runs the same fetch/analyze sequence twice for one repository. */
  jobKind?: 'analyze' | 'compare' | 'subscriptions';
}

export interface ProgressModel {
  rows: ProgressRow[];
  /** Completed work, 0..1, or null while the run has no measurable stage yet. */
  overall: number | null;
  /** The row of the most recently received event. */
  latest: ProgressRow | null;
  /** The most recent stage that has not reached a terminal state. */
  active: ProgressRow | null;
  reposSeen: number;
  reposDone: number;
  totalRepos: number | null;
}

const BASE_WEIGHT = 100; // fetch (40) + analysis (60), in stages.ts
const WRITING_WEIGHT = 10;
const TREND_WEIGHT = 10;

/** State 0=start 1=update 2=ok 3=fail, matching anim.h's ANIM_PROG_*. */
function toneOf(state: number): Tone {
  return state === 0 ? 'start' : state === 1 ? 'update' : state === 2 ? 'ok' : 'fail';
}

/** A terminal outcome outranks a transient state; the reverse never happens. */
const TONE_RANK: Record<Tone, number> = { start: 0, update: 1, ok: 2, fail: 2 };

function clamp(value: number): number {
  return Math.max(0, Math.min(1, value));
}

/** Fraction of a determinate row, or null when it has no meaningful total. */
export function rowFraction(row: ProgressRow): number | null {
  if (row.total <= 0) return null;
  return clamp(row.current / row.total);
}

/**
 * How much of the run this row represents. A completed stage earns its full
 * weight; an in-flight determinate stage earns its reported fraction; an
 * indeterminate stage earns nothing until it finishes, rather than pretending
 * that a spinner has a percentage.
 */
function rowScore(row: ProgressRow): number {
  if (row.kind === 'other' || row.tone === 'fail') return 0;
  if (row.tone === 'ok') return row.weight;
  return row.weight * (rowFraction(row) ?? 0);
}

interface RepoFold {
  rounds: ProgressRow[][];
  currentRound: number;
}

/**
 * Folds the event stream into one row per stage, in arrival order.
 *
 * The analyzer repeats its fetch/analyze sequence when compare runs two
 * periods against the same repository. A fresh START for a stage kind that has
 * already finished in the current pass therefore opens a new pass; every other
 * tick updates the existing row. That is what keeps a concurrent fetch from
 * scattering across the list while still leaving two passes distinguishable.
 */
function foldEvents(
  events: ProgressEvent[],
  translate: (raw: string) => { repo: string; stage: string },
): { rows: ProgressRow[]; folds: Map<string, RepoFold>; latest: ProgressRow | null } {
  const rows: ProgressRow[] = [];
  const folds = new Map<string, RepoFold>();
  let latest: ProgressRow | null = null;
  let eventIndex = 0;

  for (const event of events) {
    const label = translate(event.stage ?? '');
    const spec: StageSpec = stageSpec(event.stage ?? '');
    const repoKey = label.repo;
    let fold = folds.get(repoKey);
    if (!fold) {
      fold = { rounds: [[]], currentRound: 0 };
      folds.set(repoKey, fold);
    }

    let round = fold.rounds[fold.currentRound]!;
    const tone = toneOf(event.state);
    const sameKind = (row: ProgressRow) =>
      row.kind === spec.kind && (spec.kind !== 'other' || row.stage === label.stage);

    if (event.state === 0 && round.some((row) => sameKind(row) && row.tone === 'ok')) {
      const next: ProgressRow[] = [];
      fold.rounds.push(next);
      fold.currentRound = fold.rounds.length - 1;
      round = next;
    }

    const existing = round.find(sameKind);
    if (existing) {
      existing.stage = label.stage || existing.stage;
      existing.current = event.current;
      existing.total = event.total;
      existing.ticks += 1;
      existing.lastEvent = eventIndex;
      if (TONE_RANK[tone] > TONE_RANK[existing.tone]) existing.tone = tone;
      latest = existing;
      eventIndex += 1;
      continue;
    }

    const row: ProgressRow = {
      key: `${repoKey}|${spec.kind === 'other' ? label.stage : spec.kind}|${fold.currentRound}`,
      repo: label.repo,
      stage: label.stage,
      kind: spec.kind,
      order: spec.order,
      weight: spec.weight,
      current: event.current,
      total: event.total,
      tone,
      ticks: 0,
      round: fold.currentRound,
      showRepo: false,
      lastEvent: eventIndex,
    };
    round.push(row);
    rows.push(row);
    latest = row;
    eventIndex += 1;
  }

  // Print a repository once, above its first stage, rather than on every row.
  const shownRepos = new Set<string>();
  for (const row of rows) {
    if (shownRepos.has(row.repo)) continue;
    row.showRepo = true;
    shownRepos.add(row.repo);
  }

  return { rows, folds, latest };
}

/**
 * Builds the progress summary shown above the stage log.
 *
 * The denominator is known when the page supplies the repository count (batch
 * and subscriptions) or the kind (compare): that keeps the bar from jumping
 * backwards when a second repository or the second compare pass starts. For a
 * single analysis it is simply the fetch/analyze pair.
 */
export function buildProgressModel(
  events: ProgressEvent[],
  translate: (raw: string) => { repo: string; stage: string },
  options: ProgressOptions = {},
): ProgressModel {
  const { rows, folds, latest } = foldEvents(events, translate);
  const reposSeen = folds.size;

  const roundsSeen = Math.max(1, ...[...folds.values()].map((fold) => fold.rounds.length));
  const roundsPerRepo = options.jobKind === 'compare' ? 2 : roundsSeen;
  const totalRepos =
    options.totalRepos && options.totalRepos > 0 ? options.totalRepos : reposSeen || null;
  const expectedRepos = totalRepos ?? 0;

  // A trend summary is the one extra phase compare always hopes to run. Any
  // other optional phase only enters the denominator once the server emits it.
  const observedTrends = rows.filter((row) => row.kind === 'trend').length;
  const observedWritings = rows.filter((row) => row.kind === 'writing').length;
  let optionalWeight = 0;
  if (options.jobKind === 'compare') {
    optionalWeight += TREND_WEIGHT;
    if (observedTrends > 1) optionalWeight += (observedTrends - 1) * TREND_WEIGHT;
  } else {
    optionalWeight += observedTrends * TREND_WEIGHT;
  }
  optionalWeight += observedWritings * WRITING_WEIGHT;

  const expected = expectedRepos * roundsPerRepo * BASE_WEIGHT + optionalWeight;
  const score = rows.reduce((total, row) => total + rowScore(row), 0);

  let overall: number | null = events.length === 0 || expected <= 0 ? null : clamp(score / expected);
  // An older daemon may use a stage name this console does not know yet. Fall
  // back to that stage's own counter rather than showing a dead bar.
  if (!rows.some((row) => row.kind !== 'other')) {
    overall = latest && latest.total > 0 ? clamp(latest.current / latest.total) : null;
  }

  let reposDone = 0;
  for (const fold of folds.values()) {
    const complete = fold.rounds.every(
      (round) =>
        round.some((row) => row.kind === 'fetching' && row.tone === 'ok') &&
        round.some((row) => row.kind === 'analysis' && row.tone === 'ok'),
    );
    if (complete) reposDone += 1;
  }

  const active =
    rows
      .filter((row) => row.tone === 'start' || row.tone === 'update')
      .sort((a, b) => b.lastEvent - a.lastEvent)[0] ?? null;

  return {
    rows,
    overall,
    latest,
    active,
    reposSeen,
    reposDone,
    totalRepos,
  };
}
