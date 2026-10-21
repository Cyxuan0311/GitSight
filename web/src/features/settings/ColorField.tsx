import { useQuery } from '@tanstack/react-query';
import { Field, Input } from '@/components/ui/field';
import { Tooltip } from '@/components/ui/tooltip';
import { api } from '@/lib/api';
import type { NamedColor } from '@/lib/types';
import { useTranslation } from 'react-i18next';

/**
 * Colour entry matching the C side exactly: color_str_to_rgb() accepts a name
 * from a 20-entry palette, "#rrggbb", or "r,g,b". Rather than pick one, the
 * field takes free text and offers the palette as a shortcut, so a value
 * copied from the docs keeps working.
 */
export function ColorField({
  label,
  value,
  onChange,
  names,
}: {
  label: string;
  value: string;
  onChange: (next: string) => void;
  names: NamedColor[];
}) {
  const { t } = useTranslation();

  // Only offer a swatch for values that are actually a plain palette name.
  const named = names.find((entry) => entry.name === value.trim().toLowerCase());

  return (
    <Field label={label} hint={t('settings.colorHint')} className="min-w-36">
      <div className="flex gap-1">
        <span
          aria-hidden
          className="mt-0.5 size-4 shrink-0 rounded border border-line"
          style={{ background: named?.hex ?? 'transparent' }}
        />
        <Input
          value={value}
          onChange={(event) => onChange(event.target.value)}
          placeholder={t('ui.colorPlaceholder')}
          spellCheck={false}
          className="h-8 font-mono text-xs"
        />
        <Tooltip content={names.map((entry) => entry.name).join(', ')}>
          <select
            aria-label={label}
            value={named?.name ?? ''}
            onChange={(event) => {
              if (event.target.value) onChange(event.target.value);
            }}
            className="h-8 shrink-0 rounded-field border border-line-strong bg-canvas px-1 text-xs"
          >
            <option value="">—</option>
            {names.map((entry) => (
              <option key={entry.name} value={entry.name}>
                {entry.name}
              </option>
            ))}
          </select>
        </Tooltip>
      </div>
    </Field>
  );
}

/** Small helper so pages can grab the palette without a second query. */
export function useNamedColors() {
  const { data } = useQuery({ queryKey: ['meta-options'], queryFn: api.options });
  return data?.named_colors ?? [];
}