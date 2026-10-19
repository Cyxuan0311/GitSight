#ifndef GITSIGHT_CONFIG_H
#define GITSIGHT_CONFIG_H

#include "error.h"
#include "llm/client.h"

#define CONFIG_DIR  ".config/gitsight"
#define CONFIG_FILE "config.json"

#define MAX_SUBSCRIPTIONS 32
#define SUB_REPO_LEN      256
#define SUB_ALIAS_LEN      64

typedef struct {
    char  repo[SUB_REPO_LEN];
    char  alias[SUB_ALIAS_LEN];
    char  platform[16];
    int   days;
    char  format[32];
    char  branch[128];
} Subscription;

typedef struct {
    int show_prs, show_commits, show_issues, show_summary, show_top_labels;
    int show_releases, show_contributors, show_languages;
} SectionFlags;

typedef struct {
    float margin;
    float line_height;
    float col2_padding;
    unsigned char title_color[3];
    unsigned char header_color[3];
    unsigned char label_color[3];
    unsigned char link_color[3];
    unsigned char open_color[3];
    unsigned char closed_color[3];
    unsigned char merged_color[3];
} PdfExportConfig;

typedef struct {
    unsigned char title_color[3];
    unsigned char header_color[3];
    unsigned char label_color[3];
    unsigned char link_color[3];
    unsigned char open_color[3];
    unsigned char closed_color[3];
    unsigned char merged_color[3];
} HtmlExportConfig;

typedef struct {
    int   font_size;
    char  font_name[64];
    float col_a_width;
    float col_b_width;
    int   border;
    char  sheet_name[64];
} XlsxExportConfig;

typedef struct {
    char  platform[16];
    char  github_token[256];
    char  gitlab_token[256];
    char  codeberg_token[256];
    char  api_base[256];
    char  proxy[512];
    int   default_days;
    int   max_pages;
    int   color_mode;
    char  format[64];
    char  lang[16];
    char  branch[128];
    char  output[512];
    LLMConfig llms[MAX_LLMS];
    int   llm_count;
    char  active_llm[64];

    /* Export configuration */
    int   show_prs, show_commits, show_issues, show_summary, show_top_labels;
    int   show_releases, show_contributors, show_languages;
    int   item_limit;
    int   md_table_width;
    int   txt_table_width;
    int   xlsx_font_size;
    char  xlsx_font_name[64];
    float xlsx_col_a_width, xlsx_col_b_width;
    int   xlsx_border;
    char  xlsx_sheet_name[64];
    float pdf_margin, pdf_line_height, pdf_col2_padding;
    char  pdf_title_color[16];
    char  pdf_header_color[16];
    char  pdf_label_color[16];
    char  pdf_link_color[16];
    char  pdf_open_color[16];
    char  pdf_closed_color[16];
    char  pdf_merged_color[16];
    char  html_title_color[16];
    char  html_header_color[16];
    char  html_label_color[16];
    char  html_link_color[16];
    char  html_open_color[16];
    char  html_closed_color[16];
    char  html_merged_color[16];
    char  export_extra[1024];

    /* Repository subscriptions */
    Subscription subscriptions[MAX_SUBSCRIPTIONS];
    int  subscription_count;
} Config;

void        config_init(Config *cfg);
ErrorCode   config_load(Config *cfg, const char *path);
void        config_merge(Config *cfg,
                         int since_days, int max_pages, const char *format,
                         const char *output, const char *proxy,
                         const char *api_base, int color_mode,
                         const char *token, const char *llm_name,
                         const char *platform, const char *branch);
ErrorCode   config_save(const char *path, const Config *cfg);
ErrorCode   config_save_default(const char *path);
void        config_get_default_path(char *buf, size_t len);
void        config_resolve_prompts(Config *cfg, const char *config_path);
const LLMConfig *config_get_active_llm(const Config *cfg);
ErrorCode   config_set_active_llm(const char *path, const char *name);
ErrorCode   config_set_token(const char *path, const char *token);
ErrorCode   config_set(const char *path, const char *key, const char *value);

/* Create or update one LLM provider, identified by llm->name. config_set()
 * only reaches providers that already exist and covers a subset of fields,
 * which is enough for the CLI but not for a console that has to create
 * providers and edit every setting. Returns ERR_INVALID_ARGS for an unusable
 * name, an unknown protocol, or out-of-range numbers; ERR_CONFIG_WRITE when
 * the file cannot be persisted. */
ErrorCode   config_set_llm(const char *path, const LLMConfig *llm);
void        config_list_llms(const Config *cfg);

/* Subscriptions: ref matches either "owner/repo" or an alias. Returns index or -1. */
int         config_find_subscription(const Config *cfg, const char *ref);
ErrorCode   config_add_subscription(const char *path, const Subscription *sub);
ErrorCode   config_remove_subscription(const char *path, const char *ref);
ErrorCode   config_update_subscription(const char *path, const char *ref,
                                       const Subscription *changes);

#endif
