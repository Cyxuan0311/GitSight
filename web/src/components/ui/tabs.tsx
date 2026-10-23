import { Tabs as BaseTabs } from '@base-ui/react/tabs';
import { cva, type VariantProps } from 'class-variance-authority';
import { forwardRef, type ButtonHTMLAttributes } from 'react';

import { cn } from '@/lib/cn';

/**
 * The tabs root. It is the positioning context for the absolutely-taken inactive
 * panels, so switching fades one out under the other instead of tearing the
 * content out of the flow.
 */
export const Tabs = forwardRef<
  HTMLDivElement,
  React.ComponentProps<typeof BaseTabs.Root>
>(({ className, ...props }, ref) => (
  <BaseTabs.Root
    ref={ref}
    className={cn('relative', className)}
    {...(props as React.ComponentProps<typeof BaseTabs.Root>)}
  />
));
Tabs.displayName = 'Tabs';

/**
 * The tab strip.
 *
 * `hideIndicators` drops the sliding pill for a strip that mirrors another one's
 * active tab, where a second highlight would be noise.
 */
export function TabsList({
  className,
  hideIndicators,
  children,
  ...props
}: React.ComponentProps<typeof BaseTabs.List> & { hideIndicators?: boolean }) {
  return (
    <BaseTabs.List
      className={cn(
        'relative flex items-center gap-1 overflow-x-auto rounded-field border border-line bg-raised p-1 scrollbar-thin',
        className,
      )}
      {...props}
    >
      {children}
      {hideIndicators ? null : <TabsIndicator />}
    </BaseTabs.List>
  );
}

/**
 * The highlight that slides between tabs.
 *
 * Base UI measures the selected tab and publishes its box as CSS variables, so
 * the only thing left is to consume them and transition -- which is what turns
 * an instant jump into a move the eye can follow. Between tabs it slides; on
 * its very first appearance it fades in through `gs-indicator`'s starting
 * style, because there is no previous position to slide from but a jump would
 * still read as one. Height matches the list's content box rather than the
 * strip, so it sits inside the padding.
 */
function TabsIndicator({ className }: { className?: string }) {
  return (
    <BaseTabs.Indicator
      className={cn(
        'gs-indicator pointer-events-none absolute top-1 bottom-1 rounded bg-surface shadow-xs',
        'transition-[left,width,opacity] duration-200 ease-[cubic-bezier(0.32,0.72,0,1)]',
        'motion-reduce:transition-none',
        className,
      )}
      style={{
        left: 'var(--active-tab-left)',
        width: 'var(--active-tab-width)',
      }}
    />
  );
}

const tabVariants = cva(
  'relative z-1 flex items-center gap-2 whitespace-nowrap rounded px-3 py-1.5 text-xs font-medium ' +
    'transition-[color,translate] duration-150 ease-out active:translate-y-px ' +
    'motion-reduce:active:translate-y-0 [&_svg]:size-3.5',
  {
    variants: {
      selected: {
        true: 'text-ink',
        /* The muted ink lifts to full as the highlight reaches the tab, on a
         * short delay, so the colour arrives with the pill rather than ahead
         * of it. Base UI marks the active tab with data-active. */
        false: 'text-muted hover:text-ink data-[active]:text-ink data-[active]:delay-100',
      },
    },
    defaultVariants: { selected: false },
  },
);

export const Tab = forwardRef<
  HTMLButtonElement,
  ButtonHTMLAttributes<HTMLButtonElement> & VariantProps<typeof tabVariants>
>(({ className, selected, ...props }, ref) => (
  <BaseTabs.Tab
    ref={ref}
    className={cn(tabVariants({ selected: Boolean(selected) }), className)}
    {...(props as React.ComponentProps<typeof BaseTabs.Tab>)}
  />
));
Tab.displayName = 'Tab';

/**
 * Tab content.
 *
 * Panels are kept mounted and cross-faded rather than unmounted, because a mount
 * animation alone reads as a jump: the old content vanishes on one frame and the
 * new one fades in on the next, so the eye never sees a continuous move. Keeping
 * both lets the outgoing panel fade out under the incoming one, and it preserves
 * scroll position and in-flight fetches when switching back.
 *
 * The outgoing panel is only mounted while it has been visited, which is
 * `keepMounted` plus a visibility rule rather than keeping every panel alive.
 */
export function TabsPanel({
  className,
  keepMounted = true,
  ...props
}: React.ComponentProps<typeof BaseTabs.Panel>) {
  return (
    <BaseTabs.Panel
      keepMounted={keepMounted}
      className={cn(
        'outline-none',
        // Inactive panels stay in the layout so the container keeps its height,
        // but they are inert and invisible.
        'data-[hidden]:invisible data-[hidden]:pointer-events-none data-[hidden]:absolute',
        'data-[hidden]:opacity-0',
        // The arriving panel rises the last few pixels; the leaving one only
        // fades. Cancelling the animation while hidden is what replays it on
        // reveal, and the attribute selector keeps the cancellation winning
        // over the base animation no matter the class order.
        'animate-tab-in data-[hidden]:animate-none',
        'transition-opacity duration-200 ease-out motion-reduce:transition-none',
        className,
      )}
      {...props}
    />
  );
}

/** Uppercase section label used to break long pages into scannable blocks. */
export function SectionTitle({
  children,
  hint,
  action,
  className,
}: {
  children: React.ReactNode;
  hint?: React.ReactNode;
  action?: React.ReactNode;
  className?: string;
}) {
  return (
    <div className={cn('mb-3 flex items-end justify-between gap-4', className)}>
      <div className="space-y-0.5">
        <h2 className="text-sm font-semibold tracking-wide text-ink uppercase">{children}</h2>
        {hint ? <p className="text-xs text-muted">{hint}</p> : null}
      </div>
      {action}
    </div>
  );
}

/**
 * The sliding highlight behind the active tab. Base UI animates it between tabs
 * by measuring the selected one, which is why the tab strip is `relative` and
 * the tabs sit above it with `z-1`.
 */
export { BaseTabs as BaseTabsRoot };