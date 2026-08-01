#ifndef GITSIGHT_EXPORT_XLSX_H
#define GITSIGHT_EXPORT_XLSX_H

#include "../models.h"
#include "../error.h"
#include "exporter.h"
#include <stdio.h>

ErrorCode export_xlsx(FILE *fp, const Report *report,
                      const Vector *prs, const Vector *commits,
                      const Vector *issues, const Vector *releases,
                      const FormatOptions *opts);

#endif
