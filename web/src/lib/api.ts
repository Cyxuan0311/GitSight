import type {
  AnalyzeRequest,
  HttpStats,
  BatchResult,
  CacheInfo,
  CompareRequest,
  ConfigResponse,
  Health,
  Job,
  JobResult,
  JobSummary,
  LlmList,
  MetaOptions,
  Quota,
  SubscriptionBatchRequest,
  SubscriptionInput,
  SubscriptionList,
  UsageRecordRow,
  UsageResponse,
} from './types';

/** The server always answers with this shape on failure. */
interface ApiErrorBody {
  error: string;
  message: string;
  status: number;
}

export class ApiError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let response: Response;
  try {
    response = await fetch(path, {
      ...init,
      headers: {
        ...(init?.body ? { 'Content-Type': 'application/json' } : {}),
        ...init?.headers,
      },
    });
  } catch (cause) {
    // fetch only rejects on transport failures, so this means the daemon is
    // not running -- worth saying so plainly rather than showing "failed to
    // fetch" in a toast.
    throw new ApiError(0, 'network', `Cannot reach the GitSight server: ${String(cause)}`);
  }

  if (response.status === 204) return undefined as T;

  const text = await response.text();
  let body: unknown = text;
  if (text) {
    try {
      body = JSON.parse(text);
    } catch {
      /* Not JSON: keep the raw text for the error path below. */
    }
  }

  if (!response.ok) {
    const parsed = body as Partial<ApiErrorBody> | undefined;
    throw new ApiError(
      response.status,
      parsed?.error ?? 'error',
      parsed?.message ?? text ?? response.statusText,
    );
  }
  return body as T;
}

/**
 * Fetches a plain-text endpoint. `request` would hand back whatever JSON.parse
 * made of the body, which is an object whenever the server answers with a JSON
 * content type -- and an object cannot be shown in a text editor.
 */
async function requestText(path: string): Promise<string> {
  let response: Response;
  try {
    response = await fetch(path);
  } catch (cause) {
    throw new ApiError(0, 'network', `Cannot reach the GitSight server: ${String(cause)}`);
  }
  if (!response.ok) {
    let body: Partial<ApiErrorBody> | null = null;
    try {
      body = (await response.json()) as Partial<ApiErrorBody>;
    } catch {
      body = null;
    }
    throw new ApiError(
      response.status,
      body?.error ?? 'error',
      body?.message ?? response.statusText,
    );
  }
  return response.text();
}

const json = (method: string, payload?: unknown): RequestInit => ({
  method,
  ...(payload === undefined ? {} : { body: JSON.stringify(payload) }),
});

export interface RenderedExport {
  blob: Blob;
  contentType: string;
  filename: string;
  /** Decoded body for the text-based formats; undefined for pdf and xlsx. */
  text?: string;
}

/** Formats whose artifact is plain text and can be shown in the preview. */
const TEXTUAL_EXPORTS = new Set(['json', 'md', 'txt', 'html', 'csv']);

function exportUrl(jobId: string, format: string, which?: string, lang?: string): string {
  const params = new URLSearchParams({ format });
  if (which) params.set('which', which);
  // Overrides the language the job snapshotted, so a reader who switched the
  // report language gets the artifact they asked for rather than the one the
  // run happened to start in.
  if (lang) params.set('lang', lang);
  return `/api/jobs/${jobId}/export?${params}`;
}

function exportFilename(disposition: string | null, format: string): string {
  const utf8 = /filename\*=UTF-8''([^;]+)/i.exec(disposition ?? '');
  const plain = /filename="([^"]+)"/i.exec(disposition ?? '');
  try {
    return utf8 ? decodeURIComponent(utf8[1]!) : (plain?.[1] ?? `report.${format}`);
  } catch {
    return plain?.[1] ?? `report.${format}`;
  }
}

/**
 * Renders one export and hands back the artifact without downloading it, so the
 * preview can show exactly the bytes a download would produce. Nothing is
 * refetched: the daemon renders from the analysis the job already holds.
 */
export async function fetchExport(
  jobId: string,
  format: string,
  which?: string,
  lang?: string,
): Promise<RenderedExport> {
  let response: Response;
  try {
    response = await fetch(exportUrl(jobId, format, which, lang));
  } catch (cause) {
    throw new ApiError(0, 'network', `Cannot reach the GitSight server: ${String(cause)}`);
  }

  if (!response.ok) {
    const body = (await response.json().catch(() => null)) as ApiErrorBody | null;
    throw new ApiError(
      response.status,
      body?.error ?? 'error',
      body?.message ?? response.statusText,
    );
  }

  const contentType = response.headers.get('Content-Type') ?? 'application/octet-stream';
  const filename = exportFilename(response.headers.get('Content-Disposition'), format);

  // The textual formats are read as text rather than via Blob.text(): it avoids
  // buffering the same bytes twice, and Blob.text() is missing in some
  // non-browser DOM implementations.
  if (TEXTUAL_EXPORTS.has(format)) {
    const text = await response.text();
    return { blob: new Blob([text], { type: contentType }), contentType, filename, text };
  }

  return { blob: await response.blob(), contentType, filename };
}

/**
 * Triggers a browser download for a job's rendered export. The blob is fetched
 * eagerly so the server's Content-Disposition filename survives; without that
 * the browser would name every file "download".
 */
export async function downloadExport(
  jobId: string,
  format: string,
  which?: string,
  lang?: string,
): Promise<void> {
  const { blob, filename } = await fetchExport(jobId, format, which, lang);

  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  // Revoking immediately can cancel the download in some browsers.
  setTimeout(() => URL.revokeObjectURL(url), 10_000);
}

export const api = {
  downloadExportFor: downloadExport,
  fetchExport,
  /* meta */
  health: () => request<Health>('/api/health'),
  options: () => request<MetaOptions>('/api/meta/options'),
  quota: () => request<Quota>('/api/quota'),
  cache: () => request<CacheInfo>('/api/cache'),
  stats: () => request<HttpStats>('/api/meta/stats'),
  clearCache: () => request<void>('/api/cache', { method: 'DELETE' }),
  completion: (shell: 'bash' | 'zsh' | 'fish') =>
    requestText(`/api/completion/${shell}`),

  /* jobs */
  submitAnalyze: (body: AnalyzeRequest) => request<Job>('/api/jobs/analyze', json('POST', body)),
  submitCompare: (body: CompareRequest) => request<Job>('/api/jobs/compare', json('POST', body)),
  listJobs: (limit = 20) => request<{ jobs: JobSummary[]; active: number }>(`/api/jobs?limit=${limit}`),
  getJob: (id: string) => request<Job>(`/api/jobs/${id}`),
  getResult: (id: string) => request<JobResult>(`/api/jobs/${id}/result`),
  cancelJob: (id: string) => request<void>(`/api/jobs/${id}/cancel`, { method: 'POST' }),

  /* subscriptions */
  listSubscriptions: () => request<SubscriptionList>('/api/subscriptions'),
  addSubscription: (body: SubscriptionInput) =>
    request<SubscriptionList>('/api/subscriptions', json('POST', body)),
  updateSubscription: (ref: string, body: SubscriptionInput) =>
    request<SubscriptionList>(`/api/subscriptions/${encodeURIComponent(ref)}`, json('PATCH', body)),
  removeSubscription: (ref: string) =>
    request<SubscriptionList>(`/api/subscriptions/${encodeURIComponent(ref)}`, { method: 'DELETE' }),
  analyzeSubscription: (ref: string) =>
    request<Job>(`/api/subscriptions/${encodeURIComponent(ref)}/analyze`, { method: 'POST' }),
  analyzeSubscriptions: (body: SubscriptionBatchRequest = {}) =>
    request<Job>('/api/subscriptions/analyze', json('POST', body)),

  /* config */
  getConfig: () => request<ConfigResponse>('/api/config'),
  patchConfig: (sets: Record<string, string | number | boolean>) =>
    request<ConfigResponse>('/api/config', json('PATCH', { sets })),
  getRawConfig: () => requestText('/api/config/raw'),
  putRawConfig: (text: string) =>
    request<void>('/api/config/raw', { method: 'PUT', body: text }),
  resetConfig: () => request<ConfigResponse>('/api/config/reset', { method: 'POST' }),

  /* llm */
  listLlms: () => request<LlmList>('/api/llms'),
  upsertLlm: (body: Record<string, unknown>) => request<LlmList>('/api/llms', json('POST', body)),
  patchLlm: (name: string, body: Record<string, unknown>) =>
    request<LlmList>(`/api/llms/${encodeURIComponent(name)}`, json('PATCH', body)),
  deleteLlm: (name: string) =>
    request<LlmList>(`/api/llms/${encodeURIComponent(name)}`, { method: 'DELETE' }),
  activateLlm: (name: string) =>
    request<LlmList>(`/api/llms/${encodeURIComponent(name)}/activate`, { method: 'POST' }),
  testLlm: (name: string) =>
    request<{ name: string; protocol: string; ok: boolean; reply?: string; error?: string }>(
      `/api/llms/${encodeURIComponent(name)}/test`,
      { method: 'POST' },
    ),

  /* usage */
  usage: (days = 14) => request<UsageResponse>(`/api/usage?days=${days}`),
  usageRecords: (limit = 200, days = 0) =>
    request<{ total: number; returned: number; days: number; records: UsageRecordRow[] }>(
      `/api/usage/records?limit=${limit}${days > 0 ? `&days=${days}` : ''}`,
    ),
  clearUsage: () => request<void>('/api/usage', { method: 'DELETE' }),
};

export type { BatchResult };