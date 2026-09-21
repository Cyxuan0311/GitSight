#ifndef GITSIGHT_CLI_H
#define GITSIGHT_CLI_H

#include "config.h"
#include "error.h"

typedef struct {
    int    command;          // 0=analyze, 1=config, 2=version, 3=watch, 4=completion, 5=usage
    char   repos[16][256];
    int    repo_count;
    char   owner_repo[256];
    int    since_days;
    int    max_pages;
    char   format[64];
    char   output[512];
    char   platform[16];
    char   lang[16];
    char   proxy[512];
    char   api_base[256];
    char   branch[128];
    int    color_mode;
    char   config_path[1024];
    char   token[512];
    int    set_token;
    char   set_token_value[512];
    char   llm_name[64];
    char   llm_mode[16];
    int    set_llm;
    char   set_llm_value[64];
    int    list_llms;
    char   export_extra[1024];
    int    no_cache;
    int    quiet;
    int    show_config;
    int    edit_config;
    int    set_config;
    char   set_key[128];
    char   set_value[512];
    char   compare[32];
    int    parallel;           /* batch cross-repo concurrency (0 = auto) */

    /* watch (subscriptions) */
    int    watch_cmd;              /* 0=add 1=remove 2=update 3=list 4=analyze */
    char   watch_ref[256];
    char   watch_new_repo[256];
    char   watch_alias[SUB_ALIAS_LEN];
    int    watch_days;
    int    watch_days_set;
    char   watch_format[32];
    char   watch_platform[16];
    int    watch_json;

    /* completion */
    char   completion_shell[16];

    /* usage */
    int    usage_days;
    int    usage_by_llm;
    int    usage_json;
    int    usage_clear;
} Options;

ErrorCode cli_parse(int argc, char **argv, Options *opts);
void      cli_print_usage(void);

#endif
