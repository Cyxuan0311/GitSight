import { Select as BaseSelect } from '@base-ui/react/select';
import { Check, ChevronDown } from 'lucide-react';
import {
  Children,
  cloneElement,
  forwardRef,
  isValidElement,
  useId,
  type ChangeEvent,
  type InputHTMLAttributes,
  type LabelHTMLAttributes,
  type ReactElement,
  type SelectHTMLAttributes,
  type TextareaHTMLAttributes,
} from 'react';

import { cn } from '@/lib/cn';

const fieldBase =
  'w-full rounded-field border border-line-strong bg-canvas px-3 text-sm text-ink ' +
  'placeholder:text-muted transition-colors focus:border-accent focus:outline-none ' +
  'disabled:opacity-60 read-only:bg-raised';

export const Input = forwardRef<
  HTMLInputElement,
  InputHTMLAttributes<HTMLInputElement> & { invalid?: boolean }
>(({ className, invalid, ...props }, ref) => (
  <input
    ref={ref}
    className={cn(fieldBase, 'h-9', invalid && 'border-danger', className)}
    {...props}
  />
));
Input.displayName = 'Input';

export const Textarea = forwardRef<
  HTMLTextAreaElement,
  TextareaHTMLAttributes<HTMLTextAreaElement> & { invalid?: boolean }
>(({ className, invalid, ...props }, ref) => (
  <textarea
    ref={ref}
    className={cn(fieldBase, 'min-h-24 py-2 leading-relaxed', invalid && 'border-danger', className)}
    {...props}
  />
));
Textarea.displayName = 'Textarea';

export function Label({ className, ...props }: LabelHTMLAttributes<HTMLLabelElement>) {
  return (
    <label
      className={cn('block text-xs font-medium text-ink-soft', className)}
      {...props}
    />
  );
}

/** One entry of a Select, read off the `<option>` the call site passed. */
interface SelectEntry {
  value: string;
  label: React.ReactNode;
  disabled: boolean;
}

/**
 * Reads `<option>` children into data.
 *
 * The dropdown is rendered rather than the native control because a native
 * select's list is drawn by the operating system and cannot be styled: the
 * option rows, the checkmark and the popup animation are all unavailable to
 * CSS. Call sites keep writing plain `<option>` elements, so the surface they
 * use does not change.
 */
function readOptions(children: React.ReactNode): SelectEntry[] {
  const entries: SelectEntry[] = [];
  Children.forEach(children, (child) => {
    if (!isValidElement(child)) return;
    const props = child.props as {
      value?: string | number;
      disabled?: boolean;
      children?: React.ReactNode;
    };
    if (props.value === undefined) return;
    entries.push({
      value: String(props.value),
      label: props.children,
      disabled: Boolean(props.disabled),
    });
  });
  return entries;
}

export interface SelectProps
  extends Omit<
    SelectHTMLAttributes<HTMLSelectElement>,
    'onChange' | 'children' | 'value' | 'defaultValue'
  > {
  children?: React.ReactNode;
  value?: string;
  defaultValue?: string;
  /** Native-shaped handler, so react-hook-form's register() keeps working. */
  onChange?: (event: ChangeEvent<HTMLSelectElement>) => void;
  /** Preferred handler for callers holding the value themselves. */
  onValueChange?: (value: string) => void;
  /** Text shown on the trigger when nothing is selected. */
  placeholder?: string;
}

export function Select({
  children,
  value,
  defaultValue,
  onChange,
  onValueChange,
  disabled,
  name,
  id,
  required,
  className,
  placeholder,
  ...rest
}: SelectProps) {
  const options = readOptions(children);
  const selected = value ?? defaultValue ?? '';

  const choose = (next: string) => {
    onValueChange?.(next);
    // react-hook-form's register() reads target.value off the event it is given,
    // so an object with that shape is all it needs to record the choice.
    onChange?.({
      target: { name, value: next },
      type: 'change',
    } as unknown as ChangeEvent<HTMLSelectElement>);
  };

  return (
    <BaseSelect.Root
      name={name}
      value={selected}
      // The popup is portalled and only mounted while open, so without this the
      // trigger has no way to know the label and falls back to the raw value.
      items={Object.fromEntries(options.map((option) => [option.value, option.label]))}
      onValueChange={(next) => choose(next == null ? '' : String(next))}
      disabled={disabled}
      required={required}
    >
      <BaseSelect.Trigger
        id={id}
        aria-describedby={rest['aria-describedby']}
        aria-invalid={rest['aria-invalid']}
        className={cn(
          fieldBase,
          'group flex h-9 cursor-pointer items-center justify-between gap-2 text-left',
          // Base UI renders a button, so the browser's own focus ring is replaced.
          'focus-visible:border-accent focus-visible:outline-none',
          'data-[popup-open]:border-accent',
          disabled && 'cursor-not-allowed',
          className,
        )}
      >
        <BaseSelect.Value
          placeholder={placeholder}
          className="min-w-0 flex-1 truncate data-[placeholder]:text-muted"
        />
        <BaseSelect.Icon className="shrink-0 text-muted transition-transform duration-150 group-data-[popup-open]:rotate-180">
          <ChevronDown className="size-4" />
        </BaseSelect.Icon>
      </BaseSelect.Trigger>

      <BaseSelect.Portal>
        <BaseSelect.Positioner
          sideOffset={6}
          className="z-50 origin-top transition-transform duration-150 ease-out data-[ending-style]:scale-95 data-[ending-style]:opacity-0 motion-reduce:transition-none"
        >
          <BaseSelect.Popup
            className={cn(
              'animate-pop-in max-h-72 min-w-[var(--anchor-width)] overflow-hidden',
              'rounded-field border border-line bg-surface shadow-lg',
            )}
          >
            <BaseSelect.List className="max-h-72 overflow-y-auto p-1 scrollbar-thin outline-none">
              {options.map((option) => (
                <BaseSelect.Item
                  key={option.value}
                  value={option.value}
                  disabled={option.disabled}
                  onKeyDown={(event: React.KeyboardEvent<HTMLElement>) => {
                    // base-ui routes activation through a synthetic click that
                    // its own pointer guards can reject, so Enter and Space are
                    // committed here instead. Choosing the value it already has
                    // is a no-op, so a second activation cannot double-fire.
                    if (event.key !== 'Enter' && event.key !== ' ') return;
                    if (option.value === selected) return;
                    event.preventDefault();
                    event.stopPropagation();
                    choose(option.value);
                  }}
                  className={cn(
                    'flex cursor-pointer items-center justify-between gap-2 rounded px-2.5 py-1.5 text-sm',
                    'text-ink-soft select-none outline-none',
                    'data-[highlighted]:bg-accent-soft data-[highlighted]:text-ink',
                    'data-[selected]:font-medium',
                    'data-[disabled]:pointer-events-none data-[disabled]:opacity-45',
                  )}
                >
                  <BaseSelect.ItemText className="min-w-0 truncate">
                    {option.label}
                  </BaseSelect.ItemText>
                  <BaseSelect.ItemIndicator className="shrink-0 text-accent">
                    <Check className="size-3.5" />
                  </BaseSelect.ItemIndicator>
                </BaseSelect.Item>
              ))}
            </BaseSelect.List>
          </BaseSelect.Popup>
        </BaseSelect.Positioner>
      </BaseSelect.Portal>
    </BaseSelect.Root>
  );
}

interface FieldProps {
  label: React.ReactNode;
  hint?: React.ReactNode;
  error?: string;
  htmlFor?: string;
  className?: string;
  children: React.ReactNode;
}

/**
 * Label, control, and an optional hint or error.
 *
 * The label is bound to the control automatically. Handing every call site a
 * matching id and htmlFor was optional and nobody remembered it, which left the
 * app's forms with controls that screen readers could not name and that a click
 * on the label did not focus. `htmlFor` still wins for the rare caller that
 * supplies its own id.
 */
export function Field({ label, hint, error, htmlFor, className, children }: FieldProps) {
  const generatedId = useId();
  const noteId = `${generatedId}-note`;

  // Clone only when there is exactly one element to label; anything else is
  // rendered untouched rather than guessed at.
  const single =
    Children.count(children) === 1 && isValidElement(children)
      ? (children as ReactElement<{ id?: string; 'aria-describedby'?: string }>)
      : null;
  const props = single?.props ?? {};
  const controlId = htmlFor ?? props.id ?? generatedId;
  const note = error ? (
    <p id={noteId} className="text-xs text-danger">
      {error}
    </p>
  ) : hint ? (
    <p id={noteId} className="text-xs text-muted">
      {hint}
    </p>
  ) : null;

  const describedBy = [props['aria-describedby'], note ? noteId : null]
    .filter(Boolean)
    .join(' ');

  return (
    <div className={cn('space-y-1.5', className)}>
      <Label htmlFor={controlId}>{label}</Label>
      {single
        ? cloneElement(single, {
            id: controlId,
            'aria-describedby': describedBy || undefined,
          })
        : children}
      {note}
    </div>
  );
}
