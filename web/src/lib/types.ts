/**
 * TypeScript mirror of the C data model.
 *
 * Field names and optionality follow `export/json.c` exactly, including the
 * places where the exporter omits a key entirely (merged_count, avg_merge_hours,
 * repository, languages). Modelling that faithfully is what stops the UI from
 * inventing fields the backend never sends.
 */

export type PrState = 'open' | 'closed' | 'merged';

export interface PullRequest {
  number: number;
  title: string;
  state: PrState;
  created_at: string;
  merged_at: string;
  author: string;
  /** Comma-and-space joined by the driver. Use splitLabels(). */
  labels: string;
  html_url: string;
}

export interface Commit {
  /** Truncated to 7 characters by both drivers. */
  sha: string;
  message: string;
  author: string;
  date: string;
  html_url: string;
}

export interface Issue {
  number: number;
  title: string;
  state: 'open' | 'closed';
  created_at: string;
  closed_at: string;
  author: string;
  labels: string;
  html_url: string;
}

export interface Release {
  tag_name: string;
  name: string;
  author: string;
  published_at: string;
  html_url: string;
  body: string;
}

export interface ActivityBucket {
  /** "MM-DD", newest bucket first. */
  label: string;
  commits: number;
}

export interface WeekdayBucket {
  /** The exporter's rendering, in the report's language. */
  day: string;
  /** 0 = Sunday, so the console can relabel the chart in its own language. */
  index?: number;
  commits: number;
}

export interface LanguageSlice {
  name: string;
  bytes: number;
  percent: number;
}

/** Present only when repository metadata was fetched successfully. */
export interface RepoMeta {
  stars: number;
  forks: number;
  watchers: number;
  open_issues: number;
  language: string;
  license: string;
  default_branch: string;
  branch: string;
  description: string;
  created_at: string;
  pushed_at: string;
}

/** A single repository's report, as returned by the json exporter. */
export interface Report {
  owner: string;
  repo: string;
  /** Either "last N days" or an explicit "YYYY-MM-DD..YYYY-MM-DD" range. */
  period: string;
  /** Days in the rolling window; absent or 0 for an explicit date range. */
  period_days?: number;
  repository?: RepoMeta;
  pull_requests: {
    total: number;
    open: number;
    /** Includes merged, exactly as the C struct counts it. */
    closed: number;
    merged: number;
    avg_merge_hours?: number;
    merged_count?: number;
    items: PullRequest[];
  };
  commits: {
    total: number;
    authors: number;
    /** "name (N commits)"; empty when there were none. */
    top_author: string;
    activity: ActivityBucket[];
    weekday: WeekdayBucket[];
    items: Commit[];
  };
  issues: {
    total: number;
    open: number;
    closed: number;
    avg_close_hours?: number;
    closed_count?: number;
    items: Issue[];
  };
  releases: {
    total: number;
    latest: string;
    items: Release[];
  };
  contributors: {
    total: number;
    top: string;
    top_list: string;
  };
  languages?: LanguageSlice[];
  /** Comma-and-space joined; may be absent. */
  top_labels?: string;
  summary: string;

  /** Added by the server, not the exporter. */
  warnings?: Warning[];
  status?: number;
}

/** A partial resource failure. The analysis still succeeded. */
export interface Warning {
  resource: string;
  error: string;
  detail: string;
}

export interface BatchReportEntry {
  repo: string;
  status: number;
  warnings?: Warning[];
  report: Report;
}

export interface BatchResult {
  kind: 'batch';
  total: number;
  ok: number;
  failed: number;
  reports: BatchReportEntry[];
}

export interface ComparePeriod {
  pr_total: number;
  pr_merged: number;
  commit_total: number;
  author_total: number;
  issue_total: number;
  release_total: number;
  contributor_total: number;
  pr_avg_merge_hours: number;
  issue_avg_close_hours: number;
  activity_count: number;
  top_author: string;
  top_contributor: string;
  period: string;
  /** Days in this window; absent or 0 for an explicit date range. */
  period_days?: number;
  activity: Record<string, number>;
}

export interface CompareResult {
  kind: 'compare';
  owner: string;
  repo: string;
  period_days_a: number;
  period_days_b: number;
  period_a: ComparePeriod;
  period_b: ComparePeriod;
  trend_summary?: string;
  report_a: Report;
  report_b: Report;
}

export type JobResult = Report | BatchResult | CompareResult;

export function isBatchResult(value: JobResult): value is BatchResult {
  return (value as BatchResult).kind === 'batch';
}

export function isCompareResult(value: JobResult): value is CompareResult {
  return (value as CompareResult).kind === 'compare';
}

/* ---------------------------------------------------------------- meta ---- */

export interface FormatInfo {
  id: string;
  extension: string;
  mime: string;
  binary: boolean;
  exports_all_items: boolean;
  section_aware: boolean;
}

export interface PlatformInfo {
  id: string;
  api_base: string;
  auth_scheme: string;
  uses_github_driver: boolean;
}

export interface NamedColor {
  name: string;
  hex: string;
}

export interface MetaOptions {
  platforms: PlatformInfo[];
  formats: FormatInfo[];
  languages: { id: string; label: string }[];
  color_modes: string[];
  llm_modes: { id: string; hint: string }[];
  named_colors: NamedColor[];
  /** The `--export key=value` names the exporter accepts. */
  export_keys: string[];
  batch_max_threads: number;
  max_repos: number;
  max_subscriptions: number;
  max_llms: number;
  filter_max_per_kind: number;
  filter_max_len: number;
  report_langs_max: number;
}

export interface Health {
  status: string;
  version: string;
  config_path: string;
  usage_path: string;
  web_root: string;
  uptime_seconds: number;
  active_jobs: number;
}

export interface Quota {
  known: boolean;
  remaining?: number;
  reset_at?: number;
  resets_in_seconds?: number;
}

export interface CacheInfo {
  dir: string;
  ttl_seconds: number;
  enabled: boolean;
}

/* ---------------------------------------------------------------- jobs ---- */

export type JobState = 'queued' | 'running' | 'done' | 'failed' | 'cancelled';

export interface ProgressEntry {
  stage: string;
  current: number;
  total: number;
  state: number;
  at: number;
}

export interface Job {
  id: string;
  kind: 'analyze' | 'compare' | 'subscriptions';
  state: JobState;
  title: string;
  owner: string;
  repo: string;
  created_at: number;
  started_at: number;
  finished_at: number;
  progress_seq: number;
  report_count: number;
  error?: string;
  progress?: ProgressEntry[];
  cancellable: boolean;
}

export interface JobSummary {
  id: string;
  kind: Job['kind'];
  state: JobState;
  title: string;
  created_at: number;
  finished_at: number;
  progress_seq: number;
}

/* --------------------------------------------------------- config & co ---- */

/** Secret fields are reported as {set, masked} and never carry their value. */
export interface SecretFlag {
  set: boolean;
  masked: string;
}

export interface Subscription {
  index: number;
  repo: string;
  alias: string;
  platform: string;
  days: number;
  format: string;
  branch: string;
  overrides_platform: boolean;
  overrides_days: boolean;
  overrides_format: boolean;
  overrides_branch: boolean;
}

export interface ExportSettings {
  show_prs: boolean;
  show_commits: boolean;
  show_issues: boolean;
  show_summary: boolean;
  show_top_labels: boolean;
  show_releases: boolean;
  show_contributors: boolean;
  show_languages: boolean;
  item_limit: number;
  md: { table_width: number };
  txt: { table_width: number };
  xlsx: {
    font_size: number;
    font_name: string;
    col_a_width: number;
    col_b_width: number;
    border: boolean;
    sheet_name: string;
  };
  pdf: {
    margin: number;
    line_height: number;
    col2_padding: number;
    title_color: string;
    header_color: string;
    label_color: string;
    link_color: string;
    open_color: string;
    closed_color: string;
    merged_color: string;
  };
  html: {
    title_color: string;
    header_color: string;
    label_color: string;
    link_color: string;
    open_color: string;
    closed_color: string;
    merged_color: string;
  };
}

export interface ConfigResponse {
  platform: string;
  lang: string;
  api_base: string;
  proxy: string;
  default_days: number;
  max_pages: number;
  color: string;
  format: string;
  active_llm: string;
  branch: string;
  github_token: SecretFlag;
  gitlab_token: SecretFlag;
  codeberg_token: SecretFlag;
  export: ExportSettings;
  subscriptions: Subscription[];
  path: string;
  exists: boolean;
  secret_sentinel: string;
  applied?: number;
  rejected?: number;
}

export interface SubscriptionList {
  subscriptions: Subscription[];
  total: number;
  max: number;
}

export interface LlmProvider {
  name: string;
  active: boolean;
  protocol: 'openai' | 'anthropic' | 'gemini' | string;
  model: string;
  api_base: string;
  api_key: SecretFlag;
  timeout_secs: number;
  max_tokens: number;
  temperature: number;
  system_prompt_file: string;
  system_prompt_len: number;
  usable: boolean;
}

export interface LlmList {
  llms: LlmProvider[];
  active_llm: string;
  max: number;
}

export interface UsageDay {
  day: string;
  calls: number;
  prompt_tokens: number;
  completion_tokens: number;
  total_tokens: number;
}

export interface UsageGroup {
  name: string;
  calls: number;
  prompt_tokens: number;
  completion_tokens: number;
  total_tokens: number;
}

export interface UsageResponse {
  records: number;
  days: number;
  path: string;
  total: UsageGroup;
  by_day: UsageDay[];
  by_llm: UsageGroup[];
}

export interface UsageRecordRow {
  timestamp: number;
  at: string;
  llm: string;
  model: string;
  prompt_tokens: number;
  completion_tokens: number;
  total_tokens: number;
}

/* ----------------------------------------------------------- requests ---- */

export interface AnalyzeRequest {
  repo?: string;
  repos?: string[];
  repos_text?: string;
  since_days?: number;
  from?: string;
  to?: string;
  max_pages?: number;
  parallel?: number;
  filters?: string[];
  platform?: string;
  branch?: string;
  lang?: string;
  api_base?: string;
  proxy?: string;
  token?: string;
  llm?: string;
  llm_mode?: string;
  no_cache?: boolean;
  suppress_llm?: boolean;
  format?: string;
  formats?: string[];
  export?: Record<string, unknown> | string[];
}

export interface CompareRequest {
  repo: string;
  days_a: number;
  days_b: number;
  lang?: string;
  no_cache?: boolean;
  llm?: string;
  llm_mode?: string;
  formats?: string[];
  platform?: string;
  branch?: string;
  filters?: string[];
  max_pages?: number;
  api_base?: string;
  proxy?: string;
  token?: string;
  export?: Record<string, unknown> | string[];
}

export interface SubscriptionBatchRequest {
  parallel?: number;
  no_cache?: boolean;
  llm?: string;
  llm_mode?: string;
  lang?: string;
  /** Per-repository overrides, matched on repo. */
  repos?: { repo: string; since_days?: number; branch?: string }[];
}

export interface SubscriptionInput {
  repo?: string;
  alias?: string;
  platform?: string | null;
  days?: number | null;
  format?: string | null;
  branch?: string | null;
}