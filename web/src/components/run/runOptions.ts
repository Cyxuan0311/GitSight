/**
 * The per-run options that `gs analyze` accepts as flags, shared by the pages
 * that submit a job other than the single-repository form.
 *
 * Everything here is optional and blank means "keep what the config says",
 * which is the same rule the CLI follows: an unset flag never overrides the
 * file. Keeping the merge in one function is what stops compare and batch from
 * drifting apart from analyze as the flags grow.
 */
export interface RunOptionValues {
  platform: string;
  branch: string;
  lang: string;
  llm_mode: string;
  max_pages: string;
  api_base: string;
  proxy: string;
  token: string;
  filters: string;
  no_cache: boolean;
  exportOverrides: string;
}

export function emptyRunOptions(defaults?: Partial<RunOptionValues>): RunOptionValues {
  return {
    platform: '',
    branch: '',
    lang: '',
    llm_mode: '',
    max_pages: '',
    api_base: '',
    proxy: '',
    token: '',
    filters: '',
    no_cache: false,
    exportOverrides: '',
    ...defaults,
  };
}

/**
 * Reads the CLI's repeatable `--export key=value` syntax, one pair per line.
 * Returns the first offending line so the form can name it instead of silently
 * dropping an override the reader expected to apply.
 */
export function parseExportOverrides(raw: string): { pairs: string[]; invalid?: string } {
  const pairs: string[] = [];
  for (const line of raw.split('\n')) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    if (!trimmed.includes('=')) return { pairs, invalid: trimmed };
    pairs.push(trimmed);
  }
  return { pairs };
}

/** The key part of every override pair, in the order they were written. */
export function exportOverrideKeys(raw: string): string[] {
  return parseExportOverrides(raw)
    .pairs.map((pair) => pair.slice(0, pair.indexOf('=')).trim())
    .filter(Boolean);
}

export type ExportProblem =
  | { kind: 'missing_equals'; token: string }
  | { kind: 'unknown_key'; token: string };

/**
 * Reports the first problem with the override list: a line that is not a
 * key=value pair, or a key the exporter does not know. The server ignores
 * unknown keys, so naming them here is the difference between a typo and a
 * silently dropped setting.
 */
export function checkExportOverrides(raw: string, knownKeys?: string[]): ExportProblem | undefined {
  const { invalid } = parseExportOverrides(raw);
  if (invalid) return { kind: 'missing_equals', token: invalid };
  if (!knownKeys || knownKeys.length === 0) return undefined;
  const known = new Set(knownKeys);
  const unknown = exportOverrideKeys(raw).find((key) => !known.has(key));
  return unknown ? { kind: 'unknown_key', token: unknown } : undefined;
}

/** Filter entries are separated by newlines or commas on both sides. */
export function parseFilters(raw: string): string[] {
  return raw
    .split(/[\n,]+/)
    .map((entry) => entry.trim())
    .filter(Boolean);
}

export interface RunOptionRequest {
  platform?: string;
  branch?: string;
  lang?: string;
  llm_mode?: string;
  max_pages?: number;
  api_base?: string;
  proxy?: string;
  token?: string;
  filters?: string[];
  no_cache?: boolean;
  export?: string[];
}

/** Drops blanks so the server keeps the configured value for anything untouched. */
export function runOptionsToRequest(values: RunOptionValues): RunOptionRequest {
  const out: RunOptionRequest = {};

  if (values.platform) out.platform = values.platform;
  if (values.branch.trim()) out.branch = values.branch.trim();
  if (values.lang) out.lang = values.lang;
  if (values.llm_mode) out.llm_mode = values.llm_mode;

  const pages = Number(values.max_pages);
  if (values.max_pages.trim() && Number.isFinite(pages) && pages > 0) out.max_pages = pages;

  if (values.api_base.trim()) out.api_base = values.api_base.trim();
  if (values.proxy.trim()) out.proxy = values.proxy.trim();
  if (values.token.trim()) out.token = values.token.trim();

  const filters = parseFilters(values.filters);
  if (filters.length > 0) out.filters = filters;

  if (values.no_cache) out.no_cache = true;

  const overrides = parseExportOverrides(values.exportOverrides);
  if (overrides.pairs.length > 0) out.export = overrides.pairs;

  return out;
}


