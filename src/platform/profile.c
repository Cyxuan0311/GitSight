#include "gitsight/platform/profile.h"
#include <string.h>

static const PlatformProfile k_profiles[] = {
    { "github",   "https://api.github.com",        "Bearer" },
    { "codeberg", "https://codeberg.org/api/v1",    "token" },
    { "gitea",    "https://gitea.com/api/v1",       "token" },
    { "forgejo",  "https://codeberg.org/api/v1",    "token" },
};

const PlatformProfile *platform_profile(const char *name) {
    if (!name || !name[0]) return NULL;
    for (size_t i = 0; i < sizeof(k_profiles) / sizeof(k_profiles[0]); i++)
        if (strcmp(k_profiles[i].name, name) == 0)
            return &k_profiles[i];
    return NULL;
}
