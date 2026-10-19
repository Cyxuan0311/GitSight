import { useQueryClient } from '@tanstack/react-query';

import { setLanguage, type LanguageId } from '@/i18n';
import { api } from '@/lib/api';

/**
 * The console language and the report language are one setting.
 *
 * They used to be independent, which produced the failure this fixes twice over:
 * the header switched the console to English while `config.lang` stayed `zh`,
 * so every period label, summary and export the daemon rendered still came back
 * in Chinese. Writing both from a single switch keeps them from drifting apart.
 *
 * The config write is best-effort: a reader whose daemon is momentarily
 * unreachable should still get a Chinese console.
 */
export function useSyncLanguage() {
  const queryClient = useQueryClient();

  return (lang: LanguageId) => {
    setLanguage(lang);
    void api
      .patchConfig({ lang })
      .then(() => queryClient.invalidateQueries({ queryKey: ['config'] }))
      .catch(() => undefined);
  };
}

/** Applies a language outside React, for use at module scope. */
export function syncLanguage(lang: LanguageId): void {
  setLanguage(lang);
  void api.patchConfig({ lang }).catch(() => undefined);
}