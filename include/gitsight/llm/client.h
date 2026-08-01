#ifndef GITSIGHT_LLM_CLIENT_H
#define GITSIGHT_LLM_CLIENT_H

#include "../error.h"
#include <stddef.h>

#define MAX_LLMS          8
#define MAX_SYSTEM_PROMPT 4096

typedef struct {
    char name[64];
    char api_key[512];
    char api_base[512];
    char model[256];
    char protocol[16];   /* "openai" (default), "anthropic", "gemini", ... */
    char system_prompt[MAX_SYSTEM_PROMPT];
    char system_prompt_file[256];
    int  timeout_secs;
    int  max_tokens;
    float temperature;
} LLMConfig;

ErrorCode llm_generate(const LLMConfig *cfg, const char *prompt,
                       char *output, size_t output_len);

/* Resolve the effective protocol: the configured value, else auto-detected
 * from api_base (anthropic.com/claude.ai -> "anthropic",
 * generativelanguage.googleapis.com -> "gemini", otherwise "openai"). */
const char *llm_protocol(const LLMConfig *cfg);

#endif
