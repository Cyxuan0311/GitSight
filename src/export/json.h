#ifndef GS_EXPORT_JSON_H
#define GS_EXPORT_JSON_H

#include <cJSON.h>

#include "gitsight/container.h"
#include "gitsight/models.h"

/* The report as a tree.
 *
 * The daemon answers a request for a result by handing this tree straight to
 * the response, so the report is built once rather than being written out as
 * text and read back in. The caller owns the tree and releases it with
 * cJSON_Delete. */
cJSON *json_build_tree(const Report *report, const Vector *prs, const Vector *commits,
                       const Vector *issues, const Vector *releases);

#endif
