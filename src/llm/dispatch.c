#include "gitsight/llm/client.h"
#include <string.h>

ErrorCode llm_openai_generate(const LLMConfig *cfg, const char *prompt,
                              char *output, size_t output_len);
ErrorCode llm_anthropic_generate(const LLMConfig *cfg, const char *prompt,
                                 char *output, size_t output_len);
ErrorCode llm_gemini_generate(const LLMConfig *cfg, const char *prompt,
                              char *output, size_t output_len);

const char *llm_protocol(const LLMConfig *cfg) {
    if (cfg->protocol[0]) return cfg->protocol;
    if (strstr(cfg->api_base, "anthropic.com") || strstr(cfg->api_base, "claude.ai"))
        return "anthropic";
    if (strstr(cfg->api_base, "generativelanguage.googleapis.com"))
        return "gemini";
    return "openai";
}

ErrorCode llm_generate(const LLMConfig *cfg, const char *prompt,
                       char *output, size_t output_len)
{
    if (!cfg->api_key[0] || !cfg->api_base[0]) {
        return ERR_LLM_CONFIG;
    }

    const char *proto = llm_protocol(cfg);

    if (strcmp(proto, "anthropic") == 0)
        return llm_anthropic_generate(cfg, prompt, output, output_len);
    if (strcmp(proto, "gemini") == 0)
        return llm_gemini_generate(cfg, prompt, output, output_len);

    return llm_openai_generate(cfg, prompt, output, output_len);
}
