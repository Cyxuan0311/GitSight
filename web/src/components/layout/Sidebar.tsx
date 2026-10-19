import { Link } from '@tanstack/react-router';
import { PanelLeftClose, PanelLeftOpen } from 'lucide-react';
import { useTranslation } from 'react-i18next';

import { useUiStore } from '@/stores/ui';

import { cn } from '@/lib/cn';

export type IconComponent = React.ComponentType<{ className?: string }>;

export interface NavItem {
  to: string;
  labelKey: string;
  icon: IconComponent;
  /** Match the path exactly instead of by prefix. */
  exact?: boolean;
}

export interface NavGroup {
  /** Stable identity for React; labelKey is a translation key and may repeat. */
  id: string;
  /** Omitted for a heading-less group, e.g. a single top-level entry. */
  labelKey?: string;
  items: NavItem[];
}

export function Sidebar({
  groups,
  currentPath,
  onNavigate,
  variant = 'rail',
  className,
}: {
  groups: NavGroup[];
  currentPath: string;
  onNavigate?: () => void;
  /** "rail" is the collapsible desktop column; "drawer" is the mobile overlay. */
  variant?: 'rail' | 'drawer';
  className?: string;
}) {
  const { t } = useTranslation();
  const collapsed = useUiStore((state) => state.sidebarCollapsed);
  const toggleCollapsed = useUiStore((state) => state.toggleSidebarCollapsed);
  // The drawer is always fully labelled, so it ignores the collapse state.
  const compact = variant === 'rail' && collapsed;

  const isActive = (item: NavItem) =>
    item.exact ? currentPath === item.to : currentPath.startsWith(item.to);

  return (
    <aside
      className={cn(
        variant === 'rail'
          ? 'hidden shrink-0 border-r border-line bg-surface md:flex md:flex-col transition-[width] duration-200'
          : 'flex min-h-0 flex-1 flex-col overflow-hidden',
        compact ? 'w-14' : 'w-56',
        className,
      )}
    >
      <nav className="min-h-0 flex-1 space-y-5 overflow-y-auto p-3 scrollbar-thin">
        {groups.map((group) => (
          <div key={group.id} className="space-y-1">
            {compact ? (
              group.labelKey ? (
                <div className="mx-auto my-2 h-px w-6 bg-line" aria-hidden />
              ) : null
            ) : group.labelKey ? (
              <p className="px-2 pb-1 text-[10px] font-semibold tracking-wider text-muted uppercase">
                {t(group.labelKey)}
              </p>
            ) : null}
            {group.items.map((item) => {
              const active = isActive(item);
              const Icon = item.icon;
              return (
                <Link
                  key={item.to}
                  to={item.to}
                  onClick={onNavigate}
                  title={t(item.labelKey)}
                  aria-current={active ? 'page' : undefined}
                  className={cn(
                    'flex items-center gap-2.5 rounded-field px-2.5 py-2 text-sm transition-colors',
                    compact && 'justify-center px-0',
                    active
                      ? 'bg-accent-soft font-medium text-accent'
                      : 'text-ink-soft hover:bg-raised hover:text-ink',
                  )}
                >
                  <Icon className="size-4 shrink-0" />
                  {compact ? (
                    <span className="sr-only">{t(item.labelKey)}</span>
                  ) : (
                    <span className="truncate">{t(item.labelKey)}</span>
                  )}
                </Link>
              );
            })}
          </div>
        ))}
      </nav>

      {variant === 'drawer' ? null : (
      <div className="border-t border-line p-2">
        <button
          type="button"
          onClick={toggleCollapsed}
          className={cn(
            'flex w-full items-center gap-2 rounded-field px-2.5 py-2 text-xs text-muted',
            'transition-colors hover:bg-raised hover:text-ink',
            compact && 'justify-center px-0',
          )}
          aria-label={t('nav.toggle')}
          title={t('nav.toggle')}
        >
          {compact ? (
            <PanelLeftOpen className="size-4 shrink-0" />
          ) : (
            <>
              <PanelLeftClose className="size-4 shrink-0" />
              {t('nav.toggle')}
            </>
          )}
        </button>
      </div>
      )}
    </aside>
  );
}