import { useTranslation } from 'react-i18next';

import type { TFunction } from 'i18next';

/**
 * The daemon reports progress as "<repo> - <stage>" where the stage name is a
 * fixed English literal emitted by the analyzer. Splitting the two lets the
 * console label the stage in the reader's language while keeping the repository
 * exactly as it was typed.
 *
 * The stage tail is matched against a known set, so a repository name that
 * happens to contain " - " is never mistaken for a stage.
 */
const STAGE_SPECS = {
  'fetching repository data': {
    key: 'stages.fetching',
    kind: 'fetching',
    order: 0,
    // A fetch is the only phase the analyzer can report as a determinate
    // count. Its weight leaves room for the analysis that follows, so a
    // completed fetch never looks like a completed run.
    weight: 40,
  },
  'analyzing repository': {
    key: 'stages.analyzing',
    kind: 'analysis',
    order: 1,
    weight: 60,
  },
  'analyzing with llm': {
    key: 'stages.llm',
    kind: 'analysis',
    order: 1,
    weight: 60,
  },
  'writing report': {
    key: 'stages.writing',
    kind: 'writing',
    order: 2,
    weight: 10,
  },
  'summarizing trend with llm': {
    key: 'stages.trend',
    kind: 'trend',
    order: 3,
    weight: 10,
  },
} as const;

export type StageKind = (typeof STAGE_SPECS)[keyof typeof STAGE_SPECS]['kind'] | 'other';

export interface StageSpec {
  kind: StageKind;
  order: number;
  weight: number;
}

const OTHER_STAGE: StageSpec = { kind: 'other', order: Number.MAX_SAFE_INTEGER, weight: 0 };

/** The stage tail, without its "<repo> - " prefix. */
function stageName(raw: string): string {
  const separator = raw.lastIndexOf(' - ');
  return separator < 0 ? raw : raw.slice(separator + 3);
}

/** Classifies a raw stage string for progress accounting. */
export function stageSpec(raw: string): StageSpec {
  const spec = STAGE_SPECS[stageName(raw).toLowerCase() as keyof typeof STAGE_SPECS];
  return spec ? { kind: spec.kind, order: spec.order, weight: spec.weight } : OTHER_STAGE;
}

export interface StageLabel {
  repo: string;
  stage: string;
}

/**
 * Splits a raw stage string and translates the stage half when it is one of the
 * known analyzer stages. An unrecognised stage is passed through untouched, so a
 * newer daemon's stage still shows something useful.
 */
export function translateStage(raw: string, t: TFunction): StageLabel {
  const separator = raw.lastIndexOf(' - ');
  if (separator < 0) return { repo: '', stage: raw };

  const repo = raw.slice(0, separator);
  const name = raw.slice(separator + 3);
  const spec = STAGE_SPECS[name.toLowerCase() as keyof typeof STAGE_SPECS];
  return { repo, stage: spec ? t(spec.key) : name };
}

/** Convenience hook so components do not import TFunction themselves. */
export function useStageLabel() {
  const { t } = useTranslation();
  return (raw: string) => translateStage(raw, t);
}
