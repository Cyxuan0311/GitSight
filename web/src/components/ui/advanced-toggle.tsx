import { ChevronDown, SlidersHorizontal } from 'lucide-react';

import { cn } from '@/lib/cn';

/**
 * The full-width disclosure button above an optional group of fields.
 *
 * Analyze, Compare and Batch all hide their rarely-used overrides behind one of
 * these. Keeping it in one place means the height, icon and arrow animation
 * cannot drift apart between the three forms.
 */
export function AdvancedToggle({
  open,
  onToggle,
  label,
}: {
  open: boolean;
  onToggle: () => void;
  label: string;
}) {
  return (
    <button
      type="button"
      onClick={onToggle}
      aria-expanded={open}
      className="flex w-full items-center justify-between gap-2 rounded-field border border-line bg-surface px-3 py-2 text-xs font-medium text-ink-soft transition-colors hover:bg-raised hover:text-ink"
    >
      <span className="flex items-center gap-2">
        <SlidersHorizontal className="size-3.5" />
        {label}
      </span>
      <ChevronDown
        className={cn('size-3.5 transition-transform duration-150', open && 'rotate-180')}
      />
    </button>
  );
}
