import { ArrowDown, ArrowUp, Minus } from 'lucide-react';
import { useTranslation } from 'react-i18next';

import { Table, Td, Th } from '@/components/ui/feedback';
import { cn } from '@/lib/cn';
import { compareRows, type Delta } from '@/lib/format';
import type { ComparePeriod } from '@/lib/types';

/**
 * The metric table of a comparison.
 *
 * It works on the two aggregate periods alone, so the same table serves a
 * comparison of two windows of one repository and a comparison of two
 * repositories.
 */
export function CompareMetricsTable({
  a,
  b,
  labelA,
  labelB,
}: {
  a: ComparePeriod;
  b: ComparePeriod;
  /** The heading of the first column, e.g. "Period A" or a repository name. */
  labelA: string;
  labelB: string;
}) {
  const { t } = useTranslation();
  const { rows, hours } = compareRows({
    period_a: a,
    period_b: b,
  } as never);

  return (
    <Table>
      <thead>
        <tr>
          <Th>{t('compare.metric')}</Th>
          <Th className="w-28 text-right">{labelA}</Th>
          <Th className="w-28 text-right">{labelB}</Th>
          <Th className="w-24 text-right">{t('compare.delta')}</Th>
        </tr>
      </thead>
      <tbody>
        {rows.map((row) => (
          <tr key={row.key} className="hover:bg-raised/60">
            <Td className="text-ink">{t(row.labelKey)}</Td>
            <Td className="nums text-right">{row.a}</Td>
            <Td className="nums text-right">{row.b}</Td>
            <Td className="text-right">
              <DeltaCell delta={row.delta} />
            </Td>
          </tr>
        ))}
        {hours.map((row) => (
          <tr key={row.key} className="hover:bg-raised/60">
            <Td className="text-ink">{t(row.labelKey)}</Td>
            <Td className="nums text-right">{row.format(row.a)}</Td>
            <Td className="nums text-right">{row.format(row.b)}</Td>
            <Td className="text-right">
              <DeltaCell delta={row.delta} />
            </Td>
          </tr>
        ))}
      </tbody>
    </Table>
  );
}

export function DeltaCell({ delta }: { delta: Delta }) {
  const Icon =
    delta.direction === 'up' ? ArrowUp : delta.direction === 'down' ? ArrowDown : Minus;
  return (
    <span
      className={cn(
        'nums inline-flex items-center gap-1 text-xs font-medium',
        delta.direction === 'up' && 'text-open',
        delta.direction === 'down' && 'text-closed',
        delta.direction === 'flat' && 'text-muted',
        delta.direction === 'unknown' && 'text-muted',
      )}
    >
      <Icon className="size-3" />
      {delta.label}
    </span>
  );
}
