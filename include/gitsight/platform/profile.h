#ifndef GITSIGHT_PLATFORM_PROFILE_H
#define GITSIGHT_PLATFORM_PROFILE_H

/* Per-platform settings for the shared "GitHub-compatible" driver
   (github, codeberg, gitea, forgejo). These hosts expose the same REST
   surface (/repos/{owner}/{repo}/pulls|issues|commits|releases|contributors)
   and differ only in the authorization header scheme and default API base. */

typedef struct {
    const char *name;
    const char *default_api_base;
    const char *auth_scheme;
} PlatformProfile;

/* Look up a profile by platform name. Returns NULL for unknown names
   (e.g. gitlab, which uses its own driver). */
const PlatformProfile *platform_profile(const char *name);

#endif
