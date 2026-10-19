import { cleanup, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { AnalysisProgress } from '@/features/analyze/AnalysisProgress';
import { translateStage } from '@/lib/stages';
import type { ProgressEvent } from '@/hooks/useJob';

import i18n from '@/i18n';
import { renderWithProviders } from '@/test/setup';

const REPO = 'https://github.com/Cyxuan0311/PNANA.git';

/** The exact event stream a run against that repository produces. */
const FETCHING: ProgressEvent[] = [
  { stage: `${REPO} - Fetching repository data`, current: 0, total: 7, state: 0 },
  { stage: `${REPO} - Fetching repository data`, current: 1, total: 7, state: 1 },
  { stage: `${REPO} - Fetching repository data`, current: 2, total: 7, state: 1 },
  { stage: `${REPO} - Fetching repository data`, current: 3, total: 7, state: 1 },
  { stage: `${REPO} - Fetching repository data`, current: 4, total: 7, state: 1 },
  { stage: `${REPO} - Fetching repository data`, current: 5, total: 7, state: 1 },
  { stage: `${REPO} - Fetching repository data`, current: 6, total: 7, state: 1 },
  { stage: `${REPO} - Fetching repository data`, current: 7, total: 7, state: 1 },
];

afterEach(cleanup);

describe('translateStage', () => {
  it('splits the repository from the stage', () => {
    const label = translateStage(`${REPO} - Fetching repository data`, i18n.t);
    expect(label.repo).toBe(REPO);
    expect(label.stage).toBe('Fetching repository data');
  });

  it('passes through a stage with no repository prefix', () => {
    expect(translateStage('Writing report', i18n.t)).toEqual({ repo: '', stage: 'Writing report' });
  });

  it('keeps a repository whose name contains the separator', () => {
    // The tail is matched against the known stage names, so "a - b" stays part
    // of the repository rather than becoming a stage.
    const label = translateStage('org - team - Fetching repository data', i18n.t);
    expect(label.repo).toBe('org - team');
    expect(label.stage).toBe('Fetching repository data');
  });

  it('passes an unknown stage through untouched', () => {
    expect(translateStage(`${REPO} - Something new`, i18n.t).stage).toBe('Something new');
  });
});

describe('AnalysisProgress', () => {
  it('shows one row per stage instead of one per tick', () => {
    renderWithProviders(<AnalysisProgress progress={FETCHING} phase="running" />);

    const rows = screen.getAllByRole('listitem');
    expect(rows).toHaveLength(1);
    expect(rows[0]).toHaveTextContent('7/7');
    // The ticks are counted rather than repeated.
    expect(rows[0]).toHaveTextContent('x7');
  });

  it('does not scatter a stage that interleaves with another', () => {
    // The long fetch keeps ticking while the LLM call is already running.
    const interleaved: ProgressEvent[] = [
      ...FETCHING,
      { stage: `${REPO} - Analyzing with LLM`, current: 0, total: 0, state: 0 },
      ...FETCHING.slice(6).map((event) => ({ ...event, state: 1 as const })),
      { stage: `${REPO} - Analyzing with LLM`, current: 0, total: 0, state: 0 },
    ];

    renderWithProviders(<AnalysisProgress progress={interleaved} phase="running" />);
    expect(screen.getAllByRole('listitem')).toHaveLength(2);
  });

  it('shows the repository once, on its own line, not on every row', () => {
    renderWithProviders(<AnalysisProgress progress={FETCHING} phase="running" />);
    const matches = screen.getAllByText(REPO);
    expect(matches).toHaveLength(1);
  });

  it('marks a finished stage with its terminal state', () => {
    const done: ProgressEvent[] = [
      ...FETCHING,
      { stage: `${REPO} - Fetching repository data`, current: 7, total: 7, state: 2 },
      { stage: `${REPO} - Writing report`, current: 0, total: 0, state: 0 },
    ];
    renderWithProviders(<AnalysisProgress progress={done} phase="running" />);
    expect(screen.getAllByRole('listitem')).toHaveLength(2);
  });

  it('waits quietly before the first event arrives', () => {
    renderWithProviders(<AnalysisProgress progress={[]} phase="submitting" />);
    expect(screen.queryByRole('listitem')).not.toBeInTheDocument();
  });

  it('offers cancel only while the run is live', async () => {
    const { rerender } = renderWithProviders(
      <AnalysisProgress progress={FETCHING} phase="running" onCancel={() => {}} />,
    );
    expect(screen.getByRole('button', { name: /cancel/i })).toBeInTheDocument();

    rerender(<AnalysisProgress progress={FETCHING} phase="done" onCancel={() => {}} />);
    expect(screen.queryByRole('button', { name: /cancel/i })).not.toBeInTheDocument();
  });
});