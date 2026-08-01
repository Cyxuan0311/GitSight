#ifndef GITSIGHT_ANALYZER_ENGINE_H
#define GITSIGHT_ANALYZER_ENGINE_H

#include "../models.h"
#include "../llm/client.h"

void analyzer_run(Report *report, const char *owner, const char *repo,
                  int since_days, const Vector *prs,
                  const Vector *commits, const Vector *issues,
                  const Vector *releases, const Vector *contributors);

ErrorCode analyzer_llm_summary(char *summary, size_t summary_len,
                               const LLMConfig *llm,
                               const char *owner, const char *repo,
                               int since_days,
                               const Vector *prs,
                               const Vector *commits,
                               const Vector *issues,
                               const Vector *releases,
                               const char *mode);

#endif
