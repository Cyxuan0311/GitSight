#ifndef GITSIGHT_LLM_HTTP_H
#define GITSIGHT_LLM_HTTP_H

#include "gitsight/llm/client.h"

/* Internal shared HTTP transport for LLM providers. Keeps the curl/retry
 * plumbing in one place so providers only build a URL, headers and body and
 * then parse the JSON response. */

typedef struct {
    char *data;
    size_t size;
} LLMBuffer;

void llm_buffer_free(LLMBuffer *b);

/* POST `body` to `url`. `headers` is a NULL-terminated array of extra
 * "Header: value" lines (Content-Type is always added). Retries transient
 * failures (429/5xx) with backoff, up to LLM_MAX_RETRIES.
 *
 * Returns GS_OK with the NUL-terminated response body in `*out` (must be
 * freed with llm_buffer_free), or ERR_NETWORK after retries are exhausted,
 * or ERR_LLM_API/ERR_MEMORY/ERR_CURL_INIT. On a non-2xx status, prints an
 * error to stderr and sets *out_http_code. */
ErrorCode llm_http_post(const LLMConfig *cfg, const char *url,
                        const char *const headers[],
                        const char *body, LLMBuffer *out,
                        long *out_http_code);

/* Persist one token-usage record for the active provider/model. Skips when
 * GS_NO_USAGE is set. */
void llm_record_usage(const LLMConfig *cfg, const char *model,
                      long long prompt_tokens, long long completion_tokens,
                      long long total_tokens);

#endif
