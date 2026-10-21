import { useQuery } from '@tanstack/react-query';
import { useEffect } from 'react';

import { applyConfigLanguage } from '@/i18n';
import { api } from '@/lib/api';

/**
 * The export settings every report view reads: section visibility, the row
 * cap, and which platform the repository URL should point at. Kept in one
 * place so a report rendered on the analyze page, the batch page, or a
 * subscription all honours the same config.
 */
export function useReportSettings() {
  const { data } = useQuery({ queryKey: ['config'], queryFn: api.getConfig });
  const exportSettings = data?.export;

  // Every page that shows a report reads the settings through here, so this is
  // the one place the console language can adopt the configured report language.
  useEffect(() => {
    applyConfigLanguage(data?.lang);
  }, [data?.lang]);
  return {
    itemLimit: exportSettings?.item_limit ?? 10,
    showSections: {
      show_prs: exportSettings?.show_prs ?? true,
      show_commits: exportSettings?.show_commits ?? true,
      show_issues: exportSettings?.show_issues ?? true,
      show_summary: exportSettings?.show_summary ?? true,
      show_top_labels: exportSettings?.show_top_labels ?? true,
      show_releases: exportSettings?.show_releases ?? true,
      show_contributors: exportSettings?.show_contributors ?? true,
      show_languages: exportSettings?.show_languages ?? true,
    },
    platform: data?.platform ?? 'github',
    lang: data?.lang ?? 'en',
    exportSettings,
  };
}
