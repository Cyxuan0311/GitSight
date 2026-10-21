import {
  Activity,
  BarChart3,
  GitCompareArrows,
  LayoutDashboard,
  ListChecks,
  Server,
  Settings,
  Terminal,
  Users,
} from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';

import { Header } from './Header';
import { Sidebar, type NavGroup } from './Sidebar';
import { X } from 'lucide-react';

import { ToastViewport } from '@/components/ui/toast';

import { cn } from '@/lib/cn';

/**
 * Navigation is grouped by intent rather than by resource type: what you came
 * to do (analysis), what you maintain (management), what you configure
 * (system).
 */
const NAV_GROUPS: NavGroup[] = [
  {
    // Overview is a single top-level entry, so it needs no group heading.
    id: 'overview',
    items: [
      { to: '/', labelKey: 'nav.overview', icon: LayoutDashboard, exact: true },
    ],
  },
  {
    id: 'analyze',
    labelKey: 'nav.groupAnalyze',
    items: [
      { to: '/analyze', labelKey: 'nav.analyze', icon: Activity },
      { to: '/compare', labelKey: 'nav.compare', icon: GitCompareArrows },
      { to: '/batch', labelKey: 'nav.batch', icon: ListChecks },
    ],
  },
  {
    id: 'manage',
    labelKey: 'nav.groupManage',
    items: [
      { to: '/subscriptions', labelKey: 'nav.subscriptions', icon: Users },
      { to: '/usage', labelKey: 'nav.usage', icon: BarChart3 },
      { to: '/llm', labelKey: 'nav.llm', icon: Server },
    ],
  },
  {
    id: 'system',
    labelKey: 'nav.groupSystem',
    items: [
      { to: '/settings', labelKey: 'nav.settings', icon: Settings },
      { to: '/completion', labelKey: 'nav.completion', icon: Terminal },
    ],
  },
];

export function AppLayout({
  path,
  title,
  subtitle,
  actions,
  children,
}: {
  path: string;
  title: string;
  subtitle?: string;
  actions?: React.ReactNode;
  children: React.ReactNode;
}) {
  const { t } = useTranslation();
  const [drawerOpen, setDrawerOpen] = useState(false);

  return (
    <div className="flex min-h-full flex-col">
      <Header
        title={title}
        subtitle={subtitle}
        onOpenNav={() => setDrawerOpen(true)}
        actions={actions}
      />
      <div className="flex flex-1">
        <Sidebar groups={NAV_GROUPS} currentPath={path} />

        {/* Below md the rail is replaced by an overlay drawer. */}
        {drawerOpen ? (
          <div className="fixed inset-0 z-40 md:hidden">
            <button
              type="button"
              aria-label={t('app.close')}
              className="absolute inset-0 bg-black/45"
              onClick={() => setDrawerOpen(false)}
            />
            <div
              className={cn(
                'absolute inset-y-0 left-0 flex w-64 flex-col border-r border-line bg-surface',
                'shadow-xl',
              )}
            >
              <div className="flex h-14 items-center justify-between border-b border-line px-4">
                <span className="text-sm font-semibold">{t('app.name')}</span>
                <button
                  type="button"
                  onClick={() => setDrawerOpen(false)}
                  aria-label={t('app.close')}
                  className="rounded p-1 text-muted hover:bg-raised hover:text-ink"
                >
                  <X className="size-4" />
                </button>
              </div>
              <Sidebar
                groups={NAV_GROUPS}
                currentPath={path}
                onNavigate={() => setDrawerOpen(false)}
                variant="drawer"
              />
            </div>
          </div>
        ) : null}

        <main className="min-w-0 flex-1">
          <div className="mx-auto w-full max-w-[1600px] p-4 md:p-6">{children}</div>
        </main>
      </div>

      <ToastViewport />
    </div>
  );
}