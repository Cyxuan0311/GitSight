#ifndef GS_SERVER_API_UTIL_H
#define GS_SERVER_API_UTIL_H

#include "server.h"
#include <cJSON.h>

/* Translates request bodies into Options.
 *
 * Options already carries every analyze flag the CLI exposes, and the server
 * hands it straight to analyze_repo(), so there is no second set of parameters
 * to keep in sync. These helpers only fill that struct from JSON, layering
 * values in the same order the CLI does: config file, then request overrides. */

/* Seeds Options with the defaults the config file implies. */
void options_from_config(Options *o, const Config *cfg);

/* Applies an analyze request body onto `o`. Absent keys keep their current
 * value, so callers layer this over options_from_config(). Returns 0 on
 * success; on failure writes a message into `err` and returns -1. */
int  options_apply_json(const cJSON *body, Options *o, char *err, size_t errlen);

/* Layers one subscription's overrides (days/platform/format/branch) onto `o`,
 * which is what `gs watch analyze` does per entry. */
void options_apply_subscription(Options *o, const Config *cfg, const Subscription *s);

/* Extracts "k=v" pairs from a request body into a newline-joined string that
 * the Options.export_extra field expects. Accepts either an object of scalars
 * or an array of "section.key=value" strings. Returns 0 on success. */
int  options_export_overrides(const cJSON *body, char *out, size_t outlen, char *err, size_t errlen);

/* True when the value is present and not an empty string. */
int  json_str(const cJSON *obj, const char *key, char *out, size_t len);
int  json_int(const cJSON *obj, const char *key, int *out);
int  json_bool(const cJSON *obj, const char *key, int *out);

#endif /* GS_SERVER_API_UTIL_H */