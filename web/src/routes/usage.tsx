import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { BarChart3, Trash2 } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import {
  Bar,
  CartesianGrid,
  ComposedChart,
  LabelList,
  Legend,
  Line,
  ResponsiveContainer,
  Tooltip as ChartTooltip,
  XAxis,
  YAxis,
} from 'recharts';
import type { TooltipPayloadEntry } from 'recharts';

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
import { cn } from '@/lib/cn';
import { toast } from '@/stores/toast';
import { formatCompact, formatNumber, formatPercent } from '@/lib/format';
import type { UsageDay, UsageGroup } from '@/lib/types';

/** Quick windows that cover the usual "did cache help this week?" question. */
const RANGE_OPTIONS = [7, 14, 30, 90] as const;

/**
 * Token categories keep the same colour in every chart on the page: a cached
 * input token is always green, an uncached one blue, output purple. That makes
 * the daily stack and the per-provider stack readable without re-learning the
 * legend each time.
 */
const TOKEN_COLORS = {
  cached: 'var(--color-open)',
  uncached: 'var(--color-accent)',
  completion: 'var(--color-merged)',
  rate: 'var(--color-warn)',
} as const;

interface TokenDerived {
  uncached_tokens: number;
  cache_rate: number | null;
}

type DailyRow = UsageDay & TokenDerived;
type ProviderRow = UsageGroup & TokenDerived;

/**
 * Normalises the optional cache fields a log written by an older build may not
 * carry. Cached input is a subset of prompt input, so clamp it rather than
 * letting bad numbers produce a negative stack and a >100% hit rate.
 */
function withTokenBreakdown<T extends UsageDay | UsageGroup>(row: T) {
  const prompt = row.prompt_tokens ?? 0;
  const cached = Math.min(Math.max(row.cached_tokens ?? 0, 0), prompt);
  return {
    ...row,
    prompt_tokens: prompt,
    cached_tokens: cached,
    cache_write_tokens: row.cache_write_tokens ?? 0,
    uncached_tokens: prompt - cached,
    cache_rate: prompt > 0 ? cached / prompt : null,
  };
}

export function UsagePage() {
  const { t } = useTranslation();
  const [days, setDays] = useState(14);
  const [confirming, setConfirming] = useState(false);

  const queryClient = useQueryClient();
  const usage = useQuery({ queryKey: ['usage', days], queryFn: () => api.usage(days) });
  // The log below the charts follows the same window, so the two agree about
  // which period is being looked at.
  const records = useQuery({
    queryKey: ['usage-records', days],
    queryFn: () => api.usageRecords(200, days),
  });

  const clear = useMutation({
    mutationFn: api.clearUsage,
    onSuccess: () => {
      toast.success(t('usage.cleared'));
      void queryClient.invalidateQueries({ queryKey: ['usage'] });
      void queryClient.invalidateQueries({ queryKey: ['usage-records'] });
    },
    onError: (cause) =>
      toast.error(cause instanceof Error ? cause.message : t('errors.generic')),
  });

  const total = usage.data?.total;
  const byDay = usage.data?.by_day ?? [];
  const byLlm = usage.data?.by_llm ?? [];

  const dayRows: DailyRow[] = byDay.map(withTokenBreakdown);
  const providerRows: ProviderRow[] = byLlm.map(withTokenBreakdown);

  const calls = total?.calls ?? 0;
  const prompt = total?.prompt_tokens ?? 0;
  const completion = total?.completion_tokens ?? 0;
  const totalTokens = total?.total_tokens ?? 0;
  const cached = Math.min(Math.max(total?.cached_tokens ?? 0, 0), prompt);
  const uncached = prompt - cached;
  const cacheRate = prompt > 0 ? cached / prompt : 0;
  const avgPerCall = calls > 0 ? totalTokens / calls : 0;
  const perDay = totalTokens / Math.max(days, 1);
  const completionShare = totalTokens > 0 ? completion / totalTokens : 0;
  const activeDays = dayRows.filter((row) => row.calls > 0).length;
  const hasData = totalTokens > 0;
  const hasCache = cached > 0;

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
        <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-5">
          <StatCard
            label={t('usage.totalTokens')}
            value={formatCompact(totalTokens)}
            hint={t('usage.perDay', { value: formatCompact(perDay) })}
            tone="accent"
          />
          <StatCard
            label={t('usage.calls')}
            value={formatNumber(calls)}
            hint={t('usage.avgPerCallHint', { value: formatCompact(avgPerCall) })}
          />
          <StatCard
            label={t('usage.cacheHitRate')}
            value={formatPercent(cacheRate)}
            hint={t('usage.cachedOfPrompt', {
              cached: formatCompact(cached),
              prompt: formatCompact(prompt),
            })}
            tone="open"
          />
          <StatCard
            label={t('usage.prompt')}
            value={formatCompact(prompt)}
            hint={t('usage.uncachedHint', { value: formatCompact(uncached) })}
          />
          <StatCard
            label={t('usage.completion')}
            value={formatCompact(completion)}
            hint={t('usage.shareOfTotal', { value: formatPercent(completionShare) })}
          />
        </div>

        <QueryState query={usage} rows={2}>
          <div className="grid gap-4 lg:grid-cols-5">
            <Card className="lg:col-span-3">
              <CardHeader className="flex-wrap">
                <div>
                  <CardTitle>{t('usage.byDay')}</CardTitle>
                  <p className="mt-0.5 text-xs text-muted">
                    {t('usage.windowDays', { count: days })} ·{' '}
                    {t('usage.activeDays', { count: activeDays })}
                  </p>
                </div>
                <div className="flex items-center gap-1">
                  {RANGE_OPTIONS.map((value) => (
                    <Button
                      key={value}
                      size="sm"
                      variant={value === days ? 'secondary' : 'ghost'}
                      className="h-7 px-2"
                      aria-pressed={value === days}
                      onClick={() => setDays(value)}
                    >
                      {t('usage.daysShort', { count: value })}
                    </Button>
                  ))}
                </div>
              </CardHeader>
              <CardContent>
                {!hasData ? (
                  <EmptyState className="py-10" title={t('usage.noData')} />
                ) : (
                  <div className="h-72">
                    <ResponsiveContainer width="100%" height="100%">
                      <ComposedChart
                        data={dayRows}
                        margin={{ top: 16, right: hasCache ? 8 : 4, bottom: 0, left: 0 }}
                      >
                        <CartesianGrid
                          strokeDasharray="3 3"
                          stroke="var(--color-line)"
                          vertical={false}
                        />
                        <XAxis
                          dataKey="day"
                          tick={{ fontSize: 10, fill: 'var(--color-muted)' }}
                          tickFormatter={(value: string) => value.slice(5)}
                          minTickGap={18}
                          interval="preserveStartEnd"
                          axisLine={{ stroke: 'var(--color-line)' }}
                          tickLine={false}
                        />
                        <YAxis
                          yAxisId="tokens"
                          width={48}
                          tick={{ fontSize: 10, fill: 'var(--color-muted)' }}
                          tickFormatter={(value: number) => formatCompact(value)}
                          axisLine={false}
                          tickLine={false}
                        />
                        {hasCache ? (
                          <YAxis
                            yAxisId="rate"
                            orientation="right"
                            width={42}
                            domain={[0, 1]}
                            tick={{ fontSize: 10, fill: 'var(--color-muted)' }}
                            tickFormatter={(value: number) => formatPercent(value, 0)}
                            axisLine={false}
                            tickLine={false}
                          />
                        ) : null}
                        <ChartTooltip
                          cursor={{ fill: 'var(--color-raised)', opacity: 0.65 }}
                          content={<UsageTooltip />}
                        />
                        <Legend wrapperStyle={{ fontSize: 11 }} iconType="circle" iconSize={7} />
                        {hasCache ? (
                          <Bar
                            yAxisId="tokens"
                            dataKey="cached_tokens"
                            name={t('usage.cached')}
                            stackId="tokens"
                            fill={TOKEN_COLORS.cached}
                            maxBarSize={32}
                          />
                        ) : null}
                        <Bar
                          yAxisId="tokens"
                          dataKey="uncached_tokens"
                          name={t('usage.uncached')}
                          stackId="tokens"
                          fill={TOKEN_COLORS.uncached}
                          maxBarSize={32}
                        />
                        <Bar
                          yAxisId="tokens"
                          dataKey="completion_tokens"
                          name={t('usage.completion')}
                          stackId="tokens"
                          fill={TOKEN_COLORS.completion}
                          radius={[3, 3, 0, 0]}
                          maxBarSize={32}
                        >
                          {days <= 14 ? (
                            <LabelList
                              dataKey="total_tokens"
                              position="top"
                              formatter={compactLabel}
                              fill="var(--color-muted)"
                              fontSize={9}
                            />
                          ) : null}
                        </Bar>
                        {hasCache ? (
                          <Line
                            yAxisId="rate"
                            type="monotone"
                            dataKey="cache_rate"
                            name={t('usage.cacheHitRate')}
                            stroke={TOKEN_COLORS.rate}
                            strokeWidth={1.5}
                            dot={{ r: 2, strokeWidth: 0, fill: TOKEN_COLORS.rate }}
                            activeDot={{ r: 4 }}
                            connectNulls={false}
                          />
                        ) : null}
                      </ComposedChart>
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
                {providerRows.length === 0 ? (
                  <EmptyState className="py-10" title={t('usage.recordsEmpty')} />
                ) : (
                  <div className="h-72">
                    <ResponsiveContainer width="100%" height="100%">
                      <ComposedChart
                        data={providerRows}
                        layout="vertical"
                        margin={{ top: 4, right: 44, bottom: 0, left: 8 }}
                      >
                        <CartesianGrid
                          strokeDasharray="3 3"
                          stroke="var(--color-line)"
                          horizontal={false}
                        />
                        <XAxis
                          type="number"
                          tick={{ fontSize: 10, fill: 'var(--color-muted)' }}
                          tickFormatter={(value: number) => formatCompact(value)}
                          axisLine={false}
                          tickLine={false}
                        />
                        <YAxis
                          type="category"
                          dataKey="name"
                          width={86}
                          tick={{ fontSize: 11, fill: 'var(--color-muted)' }}
                          axisLine={false}
                          tickLine={false}
                        />
                        <ChartTooltip
                          cursor={{ fill: 'var(--color-raised)', opacity: 0.65 }}
                          content={<UsageTooltip />}
                        />
                        <Legend wrapperStyle={{ fontSize: 11 }} iconType="circle" iconSize={7} />
                        {hasCache ? (
                          <Bar
                            dataKey="cached_tokens"
                            name={t('usage.cached')}
                            stackId="tokens"
                            fill={TOKEN_COLORS.cached}
                            maxBarSize={26}
                          />
                        ) : null}
                        <Bar
                          dataKey="uncached_tokens"
                          name={t('usage.uncached')}
                          stackId="tokens"
                          fill={TOKEN_COLORS.uncached}
                          maxBarSize={26}
                        />
                        <Bar
                          dataKey="completion_tokens"
                          name={t('usage.completion')}
                          stackId="tokens"
                          fill={TOKEN_COLORS.completion}
                          radius={[0, 3, 3, 0]}
                          maxBarSize={26}
                        >
                          <LabelList
                            dataKey="total_tokens"
                            position="right"
                            formatter={compactLabel}
                            fill="var(--color-muted)"
                            fontSize={9}
                          />
                        </Bar>
                      </ComposedChart>
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
              <div className="flex flex-wrap items-center justify-end gap-2">
                <Badge tone="open" className="nums font-normal">
                  {t('usage.cacheHitRate')} {formatPercent(cacheRate)}
                </Badge>
                <Badge tone="neutral" className="nums font-normal">
                  {t('usage.windowDays', { count: days })}
                </Badge>
              </div>
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
                        <Th className="w-24 text-right">{t('usage.colCached')}</Th>
                        <Th className="w-24 text-right">{t('usage.colCompletion')}</Th>
                        <Th className="w-24 text-right">{t('usage.colTotal')}</Th>
                        <Th className="w-20 text-right">{t('usage.colHit')}</Th>
                      </tr>
                    </thead>
                    <tbody>
                      {records.data?.records.map((record, index) => {
                        const rowCached = Math.min(
                          Math.max(record.cached_tokens ?? 0, 0),
                          record.prompt_tokens,
                        );
                        const rowRate =
                          record.prompt_tokens > 0 ? rowCached / record.prompt_tokens : 0;
                        return (
                          <tr key={`${record.timestamp}-${index}`} className="hover:bg-raised/60">
                            <Td className="nums whitespace-nowrap text-xs">{record.at}</Td>
                            <Td className="text-xs">{record.llm}</Td>
                            <Td className="truncate font-mono text-xs">{record.model}</Td>
                            <Td className="nums text-right text-xs">
                              {formatNumber(record.prompt_tokens)}
                            </Td>
                            <Td
                              className={cn(
                                'nums text-right text-xs',
                                rowCached > 0 && 'text-open',
                              )}
                            >
                              {formatNumber(rowCached)}
                            </Td>
                            <Td className="nums text-right text-xs">
                              {formatNumber(record.completion_tokens)}
                            </Td>
                            <Td className="nums text-right text-xs font-medium text-ink">
                              {formatNumber(record.total_tokens)}
                            </Td>
                            <Td className="nums text-right text-xs">
                              {record.prompt_tokens > 0 ? formatPercent(rowRate) : '-'}
                            </Td>
                          </tr>
                        );
                      })}
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

/** A compact value for the labels drawn directly on the bars. */
function compactLabel(value: string | number | boolean | null | undefined): string {
  return formatCompact(Number(value) || 0);
}

/**
 * The default Recharts tooltip only lists the split series, which means the
 * reader has to add them up to answer "how many tokens did this day cost?".
 * This one keeps the exact counts, adds the cache hit rate, and never rounds
 * below ten thousand.
 */
function UsageTooltip({
  active,
  payload,
  label,
}: {
  active?: boolean;
  payload?: ReadonlyArray<TooltipPayloadEntry>;
  label?: string | number;
}) {
  const { t } = useTranslation();
  if (!active || !payload?.length) return null;

  const row = payload[0]?.payload as TokenDerived & {
    calls: number;
    prompt_tokens: number;
    completion_tokens: number;
    total_tokens: number;
    cached_tokens: number;
    cache_write_tokens: number;
  };
  if (!row) return null;

  const cached = row.cached_tokens ?? 0;
  const promptTokens = row.prompt_tokens ?? 0;
  const totalTokens = row.total_tokens ?? 0;
  const rate = promptTokens > 0 ? cached / promptTokens : 0;

  return (
    <div className="min-w-52 rounded-md border border-line bg-surface px-3 py-2 text-xs shadow-lg">
      <p className="font-medium text-ink">{label}</p>
      <div className="mt-1.5 space-y-1">
        <TooltipLine
          color={TOKEN_COLORS.cached}
          label={t('usage.cached')}
          value={formatNumber(cached)}
        />
        <TooltipLine
          color={TOKEN_COLORS.uncached}
          label={t('usage.uncached')}
          value={formatNumber(row.uncached_tokens ?? promptTokens - cached)}
        />
        <TooltipLine
          color={TOKEN_COLORS.completion}
          label={t('usage.completion')}
          value={formatNumber(row.completion_tokens ?? 0)}
        />
      </div>
      <div className="mt-1.5 space-y-1 border-t border-line pt-1.5">
        <TooltipLine label={t('usage.calls')} value={formatNumber(row.calls ?? 0)} />
        <TooltipLine
          label={t('usage.totalTokens')}
          value={formatNumber(totalTokens)}
          strong
        />
        <TooltipLine label={t('usage.cacheHitRate')} value={formatPercent(rate)} />
        {(row.cache_write_tokens ?? 0) > 0 ? (
          <TooltipLine
            label={t('usage.cacheWrite')}
            value={formatNumber(row.cache_write_tokens ?? 0)}
          />
        ) : null}
      </div>
    </div>
  );
}

function TooltipLine({
  color,
  label,
  value,
  strong,
}: {
  color?: string;
  label: string;
  value: string;
  strong?: boolean;
}) {
  return (
    <div className="flex items-center justify-between gap-4">
      <span className="flex items-center gap-1.5 text-muted">
        {color ? (
          <span className="size-1.5 rounded-full" style={{ background: color }} aria-hidden />
        ) : null}
        {label}
      </span>
      <span className={cn('nums text-ink', strong && 'font-semibold')}>{value}</span>
    </div>
  );
}
