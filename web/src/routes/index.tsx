import { useQuery } from '@tanstack/react-query';
import { Link, useNavigate } from '@tanstack/react-router';
import { Activity, Cpu, HardDrive, Play, Server, Timer } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';

import { AppLayout } from '@/components/layout/AppLayout';
import { LoadingBlock, QueryState } from '@/components/ErrorBoundary';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/field';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import { StatCard } from '@/components/common';
import { api } from '@/lib/api';
import { toast } from '@/stores/toast';
import { formatNumber, formatRelative } from '@/lib/format';
import { useMutation, useQueryClient } from '@tanstack/react-query';

export function OverviewPage() {
  const { t } = useTranslation();

  const health = useQuery({ queryKey: ['health'], queryFn: api.health });
  const quota = useQuery({ queryKey: ['quota'], queryFn: api.quota });
  const cache = useQuery({ queryKey: ['cache'], queryFn: api.cache });
  const config = useQuery({ queryKey: ['config'], queryFn: api.getConfig });
  const jobs = useQuery({ queryKey: ['jobs'], queryFn: () => api.listJobs(8) });
  const llms = useQuery({ queryKey: ['llms'], queryFn: api.listLlms });

  return (
    <AppLayout path="/" title={t('overview.title')} subtitle={t('overview.subtitle')}>
      <div className="space-y-4">
        <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
          <QueryState query={health}>
            <StatCard
              label={t('overview.version')}
              value={`v${health.data?.version ?? '—'}`}
              hint={
                health.data
                  ? `${t('overview.uptime')} ${formatUptime(health.data.uptime_seconds)}`
                  : undefined
              }
              icon={<Server />}
            />
          </QueryState>

          <StatCard
            label={t('overview.activeJobs')}
            value={formatNumber(health.data?.active_jobs ?? 0)}
            icon={<Activity />}
          />

          <StatCard
            label={t('overview.quota')}
            value={
              quota.data?.known ? formatNumber(quota.data.remaining ?? 0) : '—'
            }
            hint={
              quota.data?.known
                ? `${t('overview.resetsIn')} ${formatSeconds(quota.data.resets_in_seconds ?? 0)}`
                : t('overview.quotaUnknown')
            }
            title={quota.data?.known ? t('overview.quotaKnown') : undefined}
            tone={quota.data?.known && (quota.data.remaining ?? 0) < 100 ? 'merged' : 'default'}
          />

          <StatCard
            label={t('overview.llmActive')}
            value={
              llms.data?.llms.find((entry) => entry.active)?.name ?? t('overview.noLlm')
            }
            hint={config.data ? t('overview.langDefault') : undefined}
            icon={<Cpu />}
          />
        </div>

        <div className="grid gap-4 lg:grid-cols-3">
          <Card className="lg:col-span-2">
            <CardHeader>
              <CardTitle>{t('overview.quickStart')}</CardTitle>
            </CardHeader>
            <CardContent>
              <QuickStart />
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>{t('overview.cache')}</CardTitle>
              <HardDrive className="size-4 text-muted" />
            </CardHeader>
            <CardContent className="space-y-2.5">
              <QueryState query={cache} rows={2}>
                <dl className="space-y-2 text-xs">
                  <Row label={t('overview.cacheDir')}>
                    <span className="font-mono break-all">{cache.data?.dir || '-'}</span>
                  </Row>
                  <Row label={t('overview.cacheTtl')}>
                    <span className="nums">{cache.data?.ttl_seconds ?? '-'}s</span>
                  </Row>
                  <Row label={t('overview.cacheEnabled')}>
                    <Badge tone={cache.data?.enabled ? 'ok' : 'neutral'}>
                      {cache.data?.enabled ? t('app.enable') : t('app.disable')}
                    </Badge>
                  </Row>
                </dl>
                <ClearCacheButton />
              </QueryState>
            </CardContent>
          </Card>
        </div>

        <div className="grid gap-4 lg:grid-cols-3">
          <Card className="lg:col-span-2">
            <CardHeader>
              <CardTitle>{t('overview.recentJobs')}</CardTitle>
              <Button variant="ghost" size="sm" onClick={() => void jobs.refetch()}>
                {t('app.refresh')}
              </Button>
            </CardHeader>
            <CardContent className="px-0 pb-0">
<QueryState query={jobs} rows={2}>
                {(jobs.data?.jobs.length ?? 0) === 0 ? (
                  <EmptyState className="py-8" title={t('overview.noJobs')} />
                ) : (
                  <Table>
                    <thead>
                      <tr>
                        <Th>{t('report.pullRequests')}</Th>
                        <Th className="w-28">{t('state.running')}</Th>
                        <Th className="w-32 text-right">{t('usage.colTime')}</Th>
                      </tr>
                    </thead>
                    <tbody>
                      {jobs.data?.jobs.map((job) => (
                        <tr key={job.id} className="hover:bg-raised/60">
                          <Td className="truncate text-ink">
                            {/* An analysis is kept in memory by the daemon, so the
                                row is the way back into a finished report. */}
                            <Link
                              to="/analyze"
                              search={{ job: job.id }}
                              className="hover:text-accent hover:underline"
                            >
                              {job.title || job.id}
                            </Link>
                          </Td>
                          <Td>
                            <Badge
                              tone={
                                job.state === 'done'
                                  ? 'ok'
                                  : job.state === 'failed'
                                    ? 'danger'
                                    : job.state === 'running'
                                      ? 'accent'
                                      : 'neutral'
                              }
                            >
                              {t(`state.${job.state}`)}
                            </Badge>
                          </Td>
                          <Td className="nums whitespace-nowrap text-xs text-muted">
                            {formatRelative(job.created_at)}
                          </Td>
                        </tr>
                      ))}
                    </tbody>
                  </Table>
                )}
              </QueryState>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <CardTitle>{t('settings.general')}</CardTitle>
            </CardHeader>
            <CardContent>
              <QueryState query={config} rows={2}>
                <dl className="space-y-2 text-xs">
                  <Row label={t('overview.configPath')}>
                    <span className="font-mono break-all">{config.data?.path}</span>
                  </Row>
                  <Row label={t('overview.usagePath')}>
                    <span className="font-mono break-all">{health.data?.usage_path ?? '-'}</span>
                  </Row>
                  <Row label={t('overview.platformDefault')}>
                    <Badge tone="neutral">{config.data?.platform}</Badge>
                  </Row>
                  <Row label={t('settings.defaultDays')}>
                    <span className="nums">{config.data?.default_days}</span>
                  </Row>
                  <Row label={t('settings.format')}>
                    <span className="font-mono">{config.data?.format}</span>
                  </Row>
                  <Row label={t('settings.itemLimit')}>
                    <span className="nums">{config.data?.export.item_limit}</span>
                  </Row>
                </dl>
              </QueryState>
            </CardContent>
          </Card>
        </div>

        <p className="flex items-center gap-1.5 text-xs text-muted">
          <Timer className="size-3.5" />
          {t('settings.envNote')}
        </p>
      </div>
    </AppLayout>
  );
}

function QuickStart() {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const [repo, setRepo] = useState('');

  return (
    <form
      className="flex flex-wrap gap-2"
      onSubmit={(event) => {
        event.preventDefault();
        const trimmed = repo.trim();
        if (!trimmed) return;
        // The analyze form reads ?repo from the search params, so a shared link
        // works too. `search`, not `hash` -- a fragment never reaches the router.
        void navigate({ to: '/analyze', search: { repo: trimmed } });
      }}
    >
      <Input
        value={repo}
        onChange={(event) => setRepo(event.target.value)}
        placeholder={t('overview.quickStartHint')}
        className="min-w-48 flex-1"
        autoComplete="off"
        spellCheck={false}
      />
      <Button type="submit" variant="primary" disabled={!repo.trim()}>
        <Play />
        {t('overview.quickRun')}
      </Button>
    </form>
  );
}

function ClearCacheButton() {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const mutation = useMutation({
    mutationFn: api.clearCache,
    onSuccess: () => {
      toast.success(t('overview.cacheCleared'));
      void queryClient.invalidateQueries({ queryKey: ['cache'] });
    },
    onError: (cause) =>
      toast.error(cause instanceof Error ? cause.message : t('errors.generic')),
  });

  return (
    <Button
      variant="outline"
      size="sm"
      className="mt-2"
      disabled={mutation.isPending}
      onClick={() => mutation.mutate()}
    >
      {mutation.isPending ? t('app.loading') : t('overview.clearCache')}
    </Button>
  );
}

function Row({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex items-baseline justify-between gap-3">
      <dt className="shrink-0 text-muted">{label}</dt>
      <dd className="min-w-0 flex-1 text-right text-ink-soft">{children}</dd>
    </div>
  );
}

function formatUptime(seconds: number): string {
  if (seconds < 60) return `${seconds}s`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h`;
  return `${Math.floor(seconds / 86400)}d`;
}

function formatSeconds(seconds: number): string {
  if (seconds <= 0) return '—';
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `${minutes}m`;
  return `${Math.floor(minutes / 60)}h ${minutes % 60}m`;
}

export { LoadingBlock };