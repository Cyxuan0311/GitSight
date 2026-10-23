#ifndef GITSIGHT_ANALYZER_ENGINE_H
#define GITSIGHT_ANALYZER_ENGINE_H

#include "../models.h"
#include "../llm/client.h"
#include "../container.h"

void analyzer_run(Report *report, const char *owner, const char *repo,
                  int since_days, const Vector *prs,
                  const Vector *commits, const Vector *issues,
                  const Vector *releases, const Vector *contributors,
                  const Vector *langs, const Vector *churn);

ErrorCode analyzer_llm_summary(char *summary, size_t summary_len,
                               const LLMConfig *llm,
                               const char *owner, const char *repo,
                               int since_days,
                               const Vector *prs,
                               const Vector *commits,
                               const Vector *issues,
                               const Vector *releases,
                               const Report *report,
                               const char *mode);

ErrorCode analyzer_llm_compare(char *out, size_t out_len,
                               const LLMConfig *llm,
                               const char *owner, const char *repo,
                               int days_a, int days_b,
                               const Report *a, const Report *b);

#endif
