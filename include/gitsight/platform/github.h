#ifndef GITSIGHT_GITHUB_H
#define GITSIGHT_GITHUB_H

#include "../config.h"
#include "../error.h"
#include "client.h"
#include "profile.h"

/* Context for the shared "GitHub-compatible" driver. The active profile
   (github, codeberg, gitea, forgejo) is selected by cfg->platform. */
typedef struct {
    char token[256];
    char api_base[256];
    char proxy[512];
    char owner[128];
    char repo[128];
    int  timeout_secs;
    int  max_pages;
    char *error_buf;
    size_t error_len;
    const PlatformProfile *profile;
} GitHubContext;

ErrorCode github_client_init(PlatformClient *client, const Config *cfg,
                             const char *owner, const char *repo);

#endif
