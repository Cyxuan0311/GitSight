#include "gitsight/export/exporter.h"
#include "gitsight/export/xlsx.h"
#include "gitsight/export/pdf.h"
#include <string.h>

extern const Exporter JSON_EXPORTER;
extern const Exporter MD_EXPORTER;
extern const Exporter TXT_EXPORTER;

static ErrorCode write_xlsx(FILE *fp, const Report *r,
                            const Vector *prs, const Vector *commits,
                            const Vector *issues, const Vector *releases,
                            const FormatOptions *o) {
    return export_xlsx(fp, r, prs, commits, issues, releases, o);
}
static const Exporter XLSX_EXPORTER = { write_xlsx };

static ErrorCode write_pdf(FILE *fp, const Report *r,
                           const Vector *prs, const Vector *commits,
                           const Vector *issues, const Vector *releases,
                           const FormatOptions *o) {
    return export_pdf(fp, r, prs, commits, issues, releases, o);
}
static const Exporter PDF_EXPORTER = { write_pdf };

ErrorCode exporter_init(Exporter *exporter, const char *format) {
    if (strcmp(format, "json") == 0) {
        *exporter = JSON_EXPORTER;
    } else if (strcmp(format, "md") == 0) {
        *exporter = MD_EXPORTER;
    } else if (strcmp(format, "txt") == 0) {
        *exporter = TXT_EXPORTER;
    } else if (strcmp(format, "xlsx") == 0) {
        *exporter = XLSX_EXPORTER;
    } else if (strcmp(format, "pdf") == 0) {
        *exporter = PDF_EXPORTER;
    } else {
        return ERR_UNSUPPORTED_FORMAT;
    }
    return GS_OK;
}
