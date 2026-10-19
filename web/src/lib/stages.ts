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
const STAGE_KEYS: Record<string, string> = {
  'fetching repository data': 'stages.fetching',
  'analyzing repository': 'stages.analyzing',
  'analyzing with llm': 'stages.llm',
  'writing report': 'stages.writing',
  'summarizing trend with llm': 'stages.trend',
};

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
  const key = STAGE_KEYS[name.toLowerCase()];
  return { repo, stage: key ? t(key) : name };
}

/** Convenience hook so components do not import TFunction themselves. */
export function useStageLabel() {
  const { t } = useTranslation();
  return (raw: string) => translateStage(raw, t);
}