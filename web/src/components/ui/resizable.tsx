import { useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';

import { cn } from '@/lib/cn';

/**
 * Two panes separated by a divider the reader can drag.
 *
 * The report view needs far more room than the form it sits next to when a stage
 * list carries long repository names, but the useful split differs per screen,
 * so the width is remembered per key rather than hard-coded.
 *
 * The handle is a real separator: it takes focus, responds to the arrow keys and
 * reports aria-valuenow, so the layout is reachable without a pointer.
 * Double-clicking it returns to the default.
 */
export function ResizableSplit({
  side,
  children,
  min = 260,
  max = 620,
  step = 16,
  storageKey,
  defaultWidth,
  className,
}: {
  /** Leading pane, whose width the divider controls. */
  side: React.ReactNode;
  children: React.ReactNode;
  min?: number;
  max?: number;
  step?: number;
  /** Remembers the width across reloads; omit to always start at `defaultWidth`. */
  storageKey?: string;
  defaultWidth?: number;
  className?: string;
}) {
  const { t } = useTranslation();
  const start = defaultWidth ?? min;

  const [width, setWidth] = useState(() => {
    if (!storageKey) return start;
    try {
      const stored = Number(localStorage.getItem(storageKey));
      return Number.isFinite(stored) && stored >= min && stored <= max ? stored : start;
    } catch {
      return start;
    }
  });
  const [dragging, setDragging] = useState(false);
  // The divider's viewport offset, captured on pointer down.
  const originRef = useRef(0);

  const commit = useCallback(
    (value: number) => {
      const next = Math.min(max, Math.max(min, Math.round(value)));
      setWidth(next);
      if (!storageKey) return;
      try {
        localStorage.setItem(storageKey, String(next));
      } catch {
        /* A private-mode or quota failure only means the width is not remembered. */
      }
    },
    [min, max, storageKey],
  );

  useEffect(() => {
    if (!dragging) return;

    const onMove = (event: PointerEvent) => commit(event.clientX - originRef.current);
    const onUp = () => {
      setDragging(false);
      document.body.style.removeProperty('cursor');
      document.body.style.removeProperty('user-select');
    };

    window.addEventListener('pointermove', onMove);
    window.addEventListener('pointerup', onUp);
    window.addEventListener('pointercancel', onUp);
    return () => {
      window.removeEventListener('pointermove', onMove);
      window.removeEventListener('pointerup', onUp);
      window.removeEventListener('pointercancel', onUp);
    };
  }, [dragging, commit]);

  const onKeyDown = (event: React.KeyboardEvent<HTMLDivElement>) => {
    const amount = event.shiftKey ? step * 4 : step;
    switch (event.key) {
      case 'ArrowLeft':
        commit(width - amount);
        break;
      case 'ArrowRight':
        commit(width + amount);
        break;
      case 'Home':
        commit(min);
        break;
      case 'End':
        commit(max);
        break;
      default:
        return;
    }
    event.preventDefault();
  };

  return (
    <div className={cn('flex min-w-0', dragging && 'select-none', className)}>
      <div className="min-w-0 shrink-0" style={{ width }}>
        {side}
      </div>

      <div className="flex w-0 shrink-0 items-stretch">
        <div
          role="separator"
          aria-orientation="vertical"
          aria-valuenow={width}
          aria-valuemin={min}
          aria-valuemax={max}
          aria-label={t('ui.resizePanel')}
          tabIndex={0}
          data-dragging={dragging || undefined}
          onPointerDown={(event) => {
            originRef.current = event.currentTarget.getBoundingClientRect().left;
            setDragging(true);
            // Focus explicitly rather than relying on the default: a pointer
            // drag should leave the handle ready for the arrow keys.
            event.currentTarget.focus();
            // Keep the resize cursor and suppress selection over the whole page
            // so the drag does not appear to stop at the handle.
            document.body.style.cursor = 'col-resize';
            document.body.style.userSelect = 'none';
          }}
          onKeyDown={onKeyDown}
          onDoubleClick={() => commit(start)}
          className={cn(
            // touch-action none is what lets a touch drag resize instead of
            // scrolling; preventDefault on pointerdown would block focus.
            'group relative -ml-2 w-4 shrink-0 cursor-col-resize touch-none',
            'focus-visible:outline-none',
          )}
        >
          {/* The rail is always faintly visible so the boundary reads as
              grabbable, and brightens on hover, focus and drag. */}
          <span
            aria-hidden
            className={cn(
              'absolute inset-y-0 left-1/2 w-1 -translate-x-1/2 rounded-full',
              'bg-line-strong/35 transition-colors',
              'group-hover:bg-accent/60 group-focus-visible:bg-accent',
              dragging && 'bg-accent',
            )}
          />
          {/* Widens the target well past the rail so grabbing does not need
              pixel accuracy. */}
          <span aria-hidden className="absolute inset-y-0 -inset-x-2" />
        </div>
      </div>

      <div className="min-w-0 flex-1">{children}</div>

      {/* Live readout: dragging by feel alone makes it hard to tell whether the
          pane actually moved. */}
      {dragging ? (
        <div className="pointer-events-none fixed bottom-4 left-1/2 z-50 -translate-x-1/2">
          <span className="nums rounded-full border border-line bg-surface px-3 py-1 text-xs font-medium text-ink shadow-lg">
            {width}px
          </span>
        </div>
      ) : null}
    </div>
  );
}