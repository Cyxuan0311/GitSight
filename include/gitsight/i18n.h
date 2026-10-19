#ifndef GITSIGHT_I18N_H
#define GITSIGHT_I18N_H

/* Runtime language selection for report bodies (md/txt/pdf exporters).
 * The active language is set once per run from Config.lang / --lang. */
typedef enum { GS_LANG_EN = 0, GS_LANG_ZH = 1 } GsLang;

void     gs_set_lang(GsLang lang);
GsLang   gs_get_lang(void);
void     gs_set_lang_str(const char *s);   /* "zh"/"zh-CN" -> ZH, else EN */

/* Localized label for a stable id (see src/i18n.c table). Returns English
 * when no translation exists for the active language. */
const char *gs_t(const char *id);

/* Weekday name, index 0 = Sunday. */
const char *gs_weekday(int i);

/* The whole EN/ZH table as a cJSON array of {"en","zh"} objects, so a browser
 * client can render report labels in the same language the exporters use
 * without re-keying the table by hand. Caller owns the result.
 * Declared with void* to keep cJSON out of this header. */
void *gs_i18n_table_json(void);

#endif
