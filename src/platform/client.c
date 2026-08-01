#include "gitsight/platform/client.h"
#include "gitsight/platform/github.h"
#include "gitsight/platform/gitlab.h"
#include <string.h>

ErrorCode platform_client_init(PlatformClient *client, const Config *cfg,
                               const char *owner, const char *repo)
{
    if (strcmp(cfg->platform, "gitlab") == 0)
        return gitlab_client_init(client, cfg, owner, repo);
    return github_client_init(client, cfg, owner, repo);
}
