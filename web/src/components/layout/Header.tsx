import { useQuery } from '@tanstack/react-query';
import { Github, Menu, Moon, Sun } from 'lucide-react';
import { useTranslation } from 'react-i18next';

import { useSyncLanguage } from '@/lib/language';

import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Tooltip } from '@/components/ui/tooltip';
import { api } from '@/lib/api';
import { formatCompact, formatNumber } from '@/lib/format';
import { LANGUAGES, type LanguageId } from '@/i18n';
import { useUiStore } from '@/stores/ui';

import { cn } from '@/lib/cn';

export function Header({
  title,
  subtitle,
  onOpenNav,
  actions,
}: {
  title: string;
  subtitle?: string;
  onOpenNav: () => void;
  actions?: React.ReactNode;
}) {
  const { t, i18n } = useTranslation();
  // One switch for both the console and the report language; see lib/language.
  const syncLanguage = useSyncLanguage();
  const theme = useUiStore((state) => state.theme);
  const toggleTheme = useUiStore((state) => state.toggleTheme);

  const language = (i18n.language.startsWith('zh') ? 'zh' : 'en') as LanguageId;

  // Advisory and process-wide, so a stale value is preferable to a spinner.
  const { data: quota } = useQuery({
    queryKey: ['quota'],
    queryFn: api.quota,
    refetchInterval: 30_000,
  });

  return (
    <header className="sticky top-0 z-30 border-b border-line bg-canvas/85 backdrop-blur-md">
      <div className="flex h-14 items-center gap-3 px-4 md:px-6">
        <Button variant="ghost" size="icon" className="md:hidden" onClick={onOpenNav} aria-label={t('nav.toggle')}>
          <Menu />
        </Button>

        <a href="/" className="flex items-center gap-2 font-semibold text-ink">
          <span className="grid size-7 place-items-center rounded-md bg-accent text-white">
            <Github className="size-4" />
          </span>
          <span className="hidden sm:inline">{t('app.name')}</span>
        </a>

        <div className="min-w-0 flex-1">
          <h1 className="truncate text-sm font-semibold text-ink">{title}</h1>
          {subtitle ? <p className="truncate text-xs text-muted">{subtitle}</p> : null}
        </div>

        {actions}

        {quota?.known ? (
          <Tooltip
            content={`${formatNumber(quota.remaining ?? 0)} remaining · ${t('overview.resetsIn')} ${
              quota.resets_in_seconds ? formatDurationSeconds(quota.resets_in_seconds) : '—'
            }`}
          >
            <Badge tone="neutral" className="nums hidden sm:inline-flex">
              {t('overview.quota')} {formatCompact(quota.remaining ?? 0)}
            </Badge>
          </Tooltip>
        ) : null}

        <div className="flex items-center gap-1 rounded-field border border-line p-0.5">
          {LANGUAGES.map((entry) => (
            <button
              key={entry.id}
              type="button"
              onClick={() => syncLanguage(entry.id)}
              aria-pressed={language === entry.id}
              className={cn(
                'rounded px-2 py-1 text-xs font-medium transition-colors',
                language === entry.id
                  ? 'bg-surface text-ink shadow-xs'
                  : 'text-muted hover:text-ink',
              )}
            >
              {entry.id === 'zh' ? '中' : 'EN'}
            </button>
          ))}
        </div>

        <Tooltip content={theme === 'dark' ? t('settings.colorNever') : t('settings.colorAlways')}>
          <Button variant="ghost" size="icon" onClick={toggleTheme} aria-label={t('app.toggleTheme')}>
            {theme === 'dark' ? <Sun /> : <Moon />}
          </Button>
        </Tooltip>
      </div>
    </header>
  );
}

function formatDurationSeconds(seconds: number): string {
  if (seconds <= 0) return '0m';
  const hours = Math.floor(seconds / 3600);
  const minutes = Math.round((seconds % 3600) / 60);
  return hours > 0 ? `${hours}h ${minutes}m` : `${minutes}m`;
}