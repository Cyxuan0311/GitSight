import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { BarChart3, Trash2 } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Bar,
  BarChart,
  CartesianGrid,
  Cell,
  Legend,
  ResponsiveContainer,
  Tooltip as ChartTooltip,
  XAxis,
  YAxis,
} from 'recharts';

import { AppLayout } from '@/components/layout/AppLayout';
import { QueryState } from '@/components/ErrorBoundary';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/field';
import { ConfirmDialog } from '@/components/ui/modal';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import { StatCard } from '@/components/common';
import { api } from '@/lib/api';
import { formatCompact, formatNumber } from '@/lib/format';

export function UsagePage() {
  const { t } = useTranslation();
  const [days, setDays] = useState(14);
  const [confirming, setConfirming] = useState(false);

  const queryClient = useQueryClient();
  const usage = useQuery({ queryKey: ['usage', days], queryFn: () => api.usage(days) });
  const records = useQuery({ queryKey: ['usage-records'], queryFn: () => api.usageRecords(200) });

  const clear = useMutation({
    mutationFn: api.clearUsage,
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ['usage'] });
      void queryClient.invalidateQueries({ queryKey: ['usage-records'] });
    },
  });

  const total = usage.data?.total;
  const byDay = usage.data?.by_day ?? [];
  const byLlm = usage.data?.by_llm ?? [];
  const hasData = (total?.total_tokens ?? 0) > 0;

  return (
    <AppLayout
      path="/usage"
      title={t('usage.title')}
      subtitle={t('usage.subtitle')}
      actions={
        <div className="flex items-center gap-2">
          <Input
            type="number"
            min={1}
            max={365}
            value={days}
            onChange={(event) => setDays(Math.max(1, Math.min(365, Number(event.target.value) || 1)))}
            className="h-8 w-20 text-xs"
            aria-label={t('usage.window')}
          />
          <Button
            variant="outline"
            size="sm"
            disabled={!hasData}
            onClick={() => setConfirming(true)}
          >
            <Trash2 />
            {t('usage.clear')}
          </Button>
        </div>
      }
    >
      <div className="space-y-4">
        <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-4">
          <StatCard
            label={t('usage.totalTokens')}
            value={formatCompact(total?.total_tokens ?? 0)}
            tone="accent"
          />
          <StatCard label={t('usage.calls')} value={formatNumber(total?.calls ?? 0)} />
          <StatCard
            label={t('usage.prompt')}
            value={formatCompact(total?.prompt_tokens ?? 0)}
          />
          <StatCard
            label={t('usage.completion')}
            value={formatCompact(total?.completion_tokens ?? 0)}
          />
        </div>

        <QueryState query={usage} rows={2}>
          <div className="grid gap-4 lg:grid-cols-5">
            <Card className="lg:col-span-3">
              <CardHeader>
                <CardTitle>{t('usage.byDay')}</CardTitle>
                <span className="text-xs text-muted">
                  {t('usage.windowDays', { count: days })}
                </span>
              </CardHeader>
              <CardContent>
                {!hasData ? (
                  <EmptyState className="py-10" title={t('usage.noData')} />
                ) : (
                  <div className="h-64">
                    <ResponsiveContainer width="100%" height="100%">
                      <BarChart data={byDay} margin={{ top: 4, right: 4, bottom: 0, left: -16 }}>
                        <CartesianGrid strokeDasharray="3 3" stroke="var(--color-line)" vertical={false} />
                        <XAxis
                          dataKey="day"
                          tick={{ fontSize: 10, fill: 'var(--color-muted)' }}
                          tickFormatter={(value: string) => value.slice(5)}
                          axisLine={{ stroke: 'var(--color-line)' }}
                          tickLine={false}
                        />
                        <YAxis
                          tick={{ fontSize: 10, fill: 'var(--color-muted)' }}
                          axisLine={false}
                          tickLine={false}
                        />
                        <ChartTooltip
                          cursor={{ fill: 'var(--color-raised)' }}
                          contentStyle={{
                            background: 'var(--color-surface)',
                            border: '1px solid var(--color-line)',
                            borderRadius: 8,
                            fontSize: 12,
                          }}
                        />
                        <Legend wrapperStyle={{ fontSize: 11 }} />
                        <Bar
                          dataKey="prompt_tokens"
                          name={t('usage.prompt')}
                          stackId="tokens"
                          fill="var(--color-accent)"
                          radius={[0, 0, 0, 0]}
                        />
                        <Bar
                          dataKey="completion_tokens"
                          name={t('usage.completion')}
                          stackId="tokens"
                          fill="oklch(70% 0.16 150)"
                          radius={[3, 3, 0, 0]}
                        />
                      </BarChart>
                    </ResponsiveContainer>
                  </div>
                )}
              </CardContent>
            </Card>

            <Card className="lg:col-span-2">
              <CardHeader>
                <CardTitle>{t('usage.byProvider')}</CardTitle>
              </CardHeader>
              <CardContent>
                {byLlm.length === 0 ? (
                  <EmptyState className="py-10" title={t('usage.recordsEmpty')} />
                ) : (
                  <div className="h-64">
                    <ResponsiveContainer width="100%" height="100%">
                      <BarChart
                        data={byLlm}
                        layout="vertical"
                        margin={{ top: 4, right: 16, bottom: 0, left: 8 }}
                      >
                        <CartesianGrid strokeDasharray="3 3" stroke="var(--color-line)" horizontal={false} />
                        <XAxis
                          type="number"
                          tick={{ fontSize: 10, fill: 'var(--color-muted)' }}
                          axisLine={false}
                          tickLine={false}
                        />
                        <YAxis
                          type="category"
                          dataKey="name"
                          width={70}
                          tick={{ fontSize: 11, fill: 'var(--color-muted)' }}
                          axisLine={false}
                          tickLine={false}
                        />
                        <ChartTooltip
                          cursor={{ fill: 'var(--color-raised)' }}
                          contentStyle={{
                            background: 'var(--color-surface)',
                            border: '1px solid var(--color-line)',
                            borderRadius: 8,
                            fontSize: 12,
                          }}
                        />
                        <Bar dataKey="total_tokens" name={t('usage.totalTokens')} radius={[0, 3, 3, 0]}>
                          {byLlm.map((entry, index) => (
                            <Cell
                              key={entry.name}
                              fill={PROVIDER_COLORS[index % PROVIDER_COLORS.length]}
                            />
                          ))}
                        </Bar>
                      </BarChart>
                    </ResponsiveContainer>
                  </div>
                )}
              </CardContent>
            </Card>
          </div>

          <p className="mt-3 text-xs text-muted">
            {t('usage.logPath')}: <span className="font-mono break-all">{usage.data?.path}</span>
          </p>
        </QueryState>

        <Card>
          <CardHeader>
            <CardTitle>
              {t('usage.records')}
              {records.data ? (
                <span className="nums ml-2 text-xs font-normal text-muted">
                  {records.data.returned} / {records.data.total}
                </span>
              ) : null}
            </CardTitle>
            {records.data && records.data.total > 0 ? (
              <Badge tone="neutral" className="nums font-normal">
                {t('usage.windowDays', { count: 14 })}
              </Badge>
            ) : null}
          </CardHeader>
          <CardContent className="px-0 pb-0">
            <QueryState query={records} rows={2}>
              {(records.data?.records.length ?? 0) === 0 ? (
                <EmptyState
                  icon={<BarChart3 />}
                  title={t('usage.recordsEmpty')}
                  hint={t('usage.recordsEmptyHint')}
                  className="py-10"
                />
              ) : (
                <div className="max-h-96 overflow-y-auto scrollbar-thin">
                  <Table>
                    <thead className="sticky top-0 bg-surface">
                      <tr>
                        <Th className="w-40">{t('usage.colTime')}</Th>
                        <Th className="w-28">{t('usage.colProvider')}</Th>
                        <Th>{t('usage.colModel')}</Th>
                        <Th className="w-24 text-right">{t('usage.colPrompt')}</Th>
                        <Th className="w-24 text-right">{t('usage.colCompletion')}</Th>
                        <Th className="w-24 text-right">{t('usage.colTotal')}</Th>
                      </tr>
                    </thead>
                    <tbody>
                      {records.data?.records.map((record, index) => (
                        <tr key={`${record.timestamp}-${index}`} className="hover:bg-raised/60">
                          <Td className="nums whitespace-nowrap text-xs">{record.at}</Td>
                          <Td className="text-xs">{record.llm}</Td>
                          <Td className="truncate font-mono text-xs">{record.model}</Td>
                          <Td className="nums text-right text-xs">
                            {formatNumber(record.prompt_tokens)}
                          </Td>
                          <Td className="nums text-right text-xs">
                            {formatNumber(record.completion_tokens)}
                          </Td>
                          <Td className="nums text-right text-xs font-medium text-ink">
                            {formatNumber(record.total_tokens)}
                          </Td>
                        </tr>
                      ))}
                    </tbody>
                  </Table>
                </div>
              )}
            </QueryState>
          </CardContent>
        </Card>

        <ConfirmDialog
          open={confirming}
          destructive
          title={t('usage.clear')}
          message={t('usage.clearConfirm')}
          confirmLabel={t('usage.clear')}
          onCancel={() => setConfirming(false)}
          onConfirm={() => {
            setConfirming(false);
            clear.mutate();
          }}
        />
      </div>
    </AppLayout>
  );
}

const PROVIDER_COLORS = [
  'oklch(62% 0.19 250)',
  'oklch(66% 0.17 150)',
  'oklch(70% 0.18 70)',
  'oklch(62% 0.20 300)',
  'oklch(66% 0.18 20)',
  'oklch(70% 0.14 200)',
];
