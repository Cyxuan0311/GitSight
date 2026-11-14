import { X } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';

import { Header } from './Header';
import { Sidebar } from './Sidebar';
import { NAV_GROUPS } from './nav';

import { ToastViewport } from '@/components/ui/toast';

import { cn } from '@/lib/cn';

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
          {/* The page rises into place on arrival. The shell around it does not
           * animate, so switching tabs moves the content while the navigation
           * stays put. */}
          <div className="animate-tab-in mx-auto w-full max-w-[1600px] p-4 md:p-6">
            {children}
          </div>
        </main>
      </div>

      <ToastViewport />
    </div>
  );
}