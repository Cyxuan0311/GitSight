import { Popover } from '@base-ui/react/popover';
import { CalendarDays, ChevronLeft, ChevronRight, ChevronsLeft, ChevronsRight } from 'lucide-react';
import { useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';

import { cn } from '@/lib/cn';

import { fieldBase } from './control';

/**
 * A date field whose calendar and text follow the console's language.
 *
 * `<input type="date">` would be the obvious control, but its placeholder and
 * month names are drawn by the browser in the *browser's* locale. A reader who
 * switched the console to English on a Chinese system kept seeing 年/月/日, and
 * no attribute changes that. Owning the popup is what makes the language
 * setting apply here like it does everywhere else.
 *
 * The value stays an ISO `YYYY-MM-DD` string (empty for "unset"), which is the
 * same shape the analyzer and the form schema already use. All calendar maths
 * is done in UTC so a date cannot slip a day across a daylight-saving boundary.
 */
export function DateField({
  value,
  onChange,
  onBlur,
  invalid = false,
  disabled = false,
  className,
  id,
  placeholder,
  'aria-describedby': describedBy,
}: {
  /** Selected date as `YYYY-MM-DD`, or an empty string. */
  value: string;
  onChange: (value: string) => void;
  onBlur?: () => void;
  invalid?: boolean;
  disabled?: boolean;
  className?: string;
  id?: string;
  placeholder?: string;
  'aria-describedby'?: string;
}) {
  const { t, i18n } = useTranslation();
  const locale = i18n.language;

  const [open, setOpen] = useState(false);
  const selected = parseIso(value);
  const [view, setView] = useState(() => monthOf(selected ?? todayParts()));

  const today = todayParts();
  const todayIso = isoFromParts(today.year, today.month, today.day);
  const first = firstDayOfWeek(locale);

  const display = useMemo(() => {
    const parts = parseIso(value);
    if (!parts) return placeholder ?? t('ui.pickDate');
    return new Intl.DateTimeFormat(locale, {
      dateStyle: 'medium',
      timeZone: 'UTC',
    }).format(new Date(Date.UTC(parts.year, parts.month - 1, parts.day)));
  }, [value, locale, placeholder, t]);

  const monthLabel = useMemo(
    () =>
      new Intl.DateTimeFormat(locale, {
        month: 'long',
        year: 'numeric',
        timeZone: 'UTC',
      }).format(new Date(Date.UTC(view.year, view.month - 1, 1))),
    [locale, view.year, view.month],
  );

  const weekdays = useMemo(() => {
    const format = new Intl.DateTimeFormat(locale, { weekday: 'narrow', timeZone: 'UTC' });
    return Array.from({ length: 7 }, (_, index) => {
      // 2024-01-07 was a Sunday, so shifting by the first weekday of the locale
      // produces the labels in the order the grid below draws them.
      const day = new Date(Date.UTC(2024, 0, 7 + ((first + index) % 7)));
      return format.format(day);
    });
  }, [locale, first]);

  const fullDate = useMemo(
    () => new Intl.DateTimeFormat(locale, { dateStyle: 'full', timeZone: 'UTC' }),
    [locale],
  );

  const cells = useMemo(() => monthCells(view.year, view.month, first), [view.year, view.month, first]);

  const shiftMonth = (delta: number) => {
    setView((current) => {
      const next = new Date(Date.UTC(current.year, current.month - 1 + delta, 1));
      return { year: next.getUTCFullYear(), month: next.getUTCMonth() + 1 };
    });
  };

  const pick = (day: number) => {
    onChange(isoFromParts(view.year, view.month, day));
    setOpen(false);
  };

  return (
    <Popover.Root
      open={open}
      onOpenChange={(next) => {
        if (next) {
          // Re-open on the month that holds the current value rather than
          // wherever the last visit left the calendar.
          setView(monthOf(parseIso(value) ?? todayParts()));
        }
        setOpen(next);
      }}
    >
      <Popover.Trigger
        id={id}
        type="button"
        disabled={disabled}
        aria-invalid={invalid || undefined}
        aria-describedby={describedBy}
        onBlur={onBlur}
        className={cn(
          fieldBase,
          'group flex h-9 cursor-pointer items-center justify-between gap-2 text-left',
          'focus-visible:border-accent focus-visible:outline-none',
          'data-[popup-open]:border-accent',
          invalid && 'border-danger',
          disabled && 'cursor-not-allowed',
          className,
        )}
      >
        <span className={cn('truncate', !selected && 'text-muted')}>{display}</span>
        <CalendarDays className="size-4 shrink-0 text-muted" />
      </Popover.Trigger>

      <Popover.Portal>
        <Popover.Positioner sideOffset={6} className="z-50">
          <Popover.Popup className="animate-pop-in w-72 rounded-card border border-line bg-surface p-3 shadow-lg">
            <div className="flex items-center justify-between gap-1">
              <div className="flex items-center">
                <button
                  type="button"
                  aria-label={t('ui.previousYear')}
                  onClick={() => shiftMonth(-12)}
                  className="rounded p-1 text-muted transition-colors hover:bg-raised hover:text-ink"
                >
                  <ChevronsLeft className="size-4" />
                </button>
                <button
                  type="button"
                  aria-label={t('ui.previousMonth')}
                  onClick={() => shiftMonth(-1)}
                  className="rounded p-1 text-muted transition-colors hover:bg-raised hover:text-ink"
                >
                  <ChevronLeft className="size-4" />
                </button>
              </div>
              <span className="truncate text-xs font-medium text-ink" aria-live="polite">
                {monthLabel}
              </span>
              <div className="flex items-center">
                <button
                  type="button"
                  aria-label={t('ui.nextMonth')}
                  onClick={() => shiftMonth(1)}
                  className="rounded p-1 text-muted transition-colors hover:bg-raised hover:text-ink"
                >
                  <ChevronRight className="size-4" />
                </button>
                <button
                  type="button"
                  aria-label={t('ui.nextYear')}
                  onClick={() => shiftMonth(12)}
                  className="rounded p-1 text-muted transition-colors hover:bg-raised hover:text-ink"
                >
                  <ChevronsRight className="size-4" />
                </button>
              </div>
            </div>

            <div className="mt-2 grid grid-cols-7 text-center text-[10px] font-medium tracking-wide text-muted uppercase">
              {weekdays.map((label, index) => (
                <span key={index} aria-hidden>
                  {label}
                </span>
              ))}
            </div>

            <div className="mt-1 grid grid-cols-7 gap-1">
              {cells.map((day, index) => {
                if (day === null) {
                  return <span key={index} aria-hidden />;
                }
                const isSelected =
                  selected?.year === view.year &&
                  selected.month === view.month &&
                  selected.day === day;
                const isToday =
                  today.year === view.year && today.month === view.month && today.day === day;
                return (
                  <button
                    key={index}
                    type="button"
                    aria-label={fullDate.format(new Date(Date.UTC(view.year, view.month - 1, day)))}
                    aria-current={isSelected ? 'date' : undefined}
                    onClick={() => pick(day)}
                    className={cn(
                      'nums size-8 rounded-field text-xs transition-colors',
                      isSelected
                        ? 'bg-accent font-medium text-white hover:bg-accent-hover'
                        : isToday
                          ? 'border border-accent/60 text-accent hover:bg-accent-soft'
                          : 'text-ink-soft hover:bg-raised hover:text-ink',
                    )}
                  >
                    {day}
                  </button>
                );
              })}
            </div>

            <div className="mt-3 flex items-center justify-between border-t border-line pt-2">
              <button
                type="button"
                onClick={() => {
                  onChange(todayIso);
                  setOpen(false);
                }}
                className="rounded px-1 py-0.5 text-xs font-medium text-accent transition-colors hover:underline"
              >
                {t('ui.today')}
              </button>
              <button
                type="button"
                disabled={!value}
                onClick={() => {
                  onChange('');
                  setOpen(false);
                }}
                className="rounded px-1 py-0.5 text-xs text-muted transition-colors hover:text-ink disabled:opacity-40"
              >
                {t('ui.clearDate')}
              </button>
            </div>
          </Popover.Popup>
        </Popover.Positioner>
      </Popover.Portal>
    </Popover.Root>
  );
}

const pad = (value: number) => String(value).padStart(2, '0');

function isoFromParts(year: number, month: number, day: number): string {
  return `${String(year).padStart(4, '0')}-${pad(month)}-${pad(day)}`;
}

function parseIso(value: string): { year: number; month: number; day: number } | null {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(value);
  if (!match) return null;
  const year = Number(match[1]);
  const month = Number(match[2]);
  const day = Number(match[3]);
  const probe = new Date(Date.UTC(year, month - 1, day));
  if (
    probe.getUTCFullYear() !== year ||
    probe.getUTCMonth() !== month - 1 ||
    probe.getUTCDate() !== day
  ) {
    return null;
  }
  return { year, month, day };
}

function todayParts(): { year: number; month: number; day: number } {
  const now = new Date();
  return { year: now.getFullYear(), month: now.getMonth() + 1, day: now.getDate() };
}

function monthOf(parts: { year: number; month: number }): { year: number; month: number } {
  return { year: parts.year, month: parts.month };
}

/**
 * The locale's first day of the week, as a JavaScript weekday index.
 *
 * `Intl.Locale#weekInfo` is not in every engine the console supports, so an
 * unsupported locale falls back to Sunday rather than failing to draw a grid.
 */
function firstDayOfWeek(locale: string): number {
  try {
    const info = new Intl.Locale(locale) as Intl.Locale & { weekInfo?: { firstDay: number } };
    const firstDay = info.weekInfo?.firstDay;
    // Intl counts 1 = Monday ... 7 = Sunday; Date counts 0 = Sunday.
    if (firstDay) return firstDay % 7;
  } catch {
    /* Older engines: use the fallback below. */
  }
  return 0;
}

/** Days of a month, padded with nulls so every row has seven cells. */
function monthCells(year: number, month: number, first: number): (number | null)[] {
  const firstWeekday = new Date(Date.UTC(year, month - 1, 1)).getUTCDay();
  const daysInMonth = new Date(Date.UTC(year, month, 0)).getUTCDate();
  const cells: (number | null)[] = new Array<number | null>(
    (firstWeekday - first + 7) % 7,
  ).fill(null);
  for (let day = 1; day <= daysInMonth; day += 1) cells.push(day);
  while (cells.length % 7 !== 0) cells.push(null);
  return cells;
}
