#ifndef GITSIGHT_PLATFORM_CLIENT_H
#define GITSIGHT_PLATFORM_CLIENT_H

#include "../config.h"
#include "../container.h"
#include "../models.h"
#include "../error.h"

typedef struct PlatformClient {
    char owner[128];
    char repo[128];
    char api_base[256];
    char proxy[512];
    int  timeout_secs;
    void *ctx;

    ErrorCode (*fetch_prs)(void *ctx, Vector *prs, int since_days);
    ErrorCode (*fetch_commits)(void *ctx, Vector *commits, int since_days);
    ErrorCode (*fetch_issues)(void *ctx, Vector *issues, int since_days);
    ErrorCode (*fetch_releases)(void *ctx, Vector *releases, int since_days);
    ErrorCode (*fetch_meta)(void *ctx, RepoMeta *meta);
    ErrorCode (*fetch_contributors)(void *ctx, Vector *contributors);
    char      last_error[512];
    void      (*destroy)(void *ctx);
} PlatformClient;

ErrorCode platform_client_init(PlatformClient *client, const Config *cfg,
                               const char *owner, const char *repo);

#endif
