import { act, cleanup, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { DateField } from './date-field';
import i18n from '@/i18n';
import { renderWithProviders } from '@/test/setup';

afterEach(async () => {
  cleanup();
  await act(async () => {
    await i18n.changeLanguage('en');
  });
});

async function switchLanguage(lang: 'en' | 'zh') {
  await act(async () => {
    await i18n.changeLanguage(lang);
  });
}

function displayDate(iso: string, lang = i18n.language): string {
  const [year = 0, month = 1, day = 1] = iso.split('-').map(Number);
  return new Intl.DateTimeFormat(lang, { dateStyle: 'medium', timeZone: 'UTC' }).format(
    new Date(Date.UTC(year, month - 1, day)),
  );
}

function todayIso(): string {
  const now = new Date();
  const pad = (value: number) => String(value).padStart(2, '0');
  return `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`;
}

describe('DateField', () => {
  it('shows its placeholder in the console language, not the browser locale', async () => {
    renderWithProviders(<DateField value="" onChange={() => undefined} />);

    expect(screen.getByRole('button')).toHaveTextContent('Pick a date');

    await switchLanguage('zh');

    expect(screen.getByRole('button')).toHaveTextContent('选择日期');
    expect(screen.queryByText('Pick a date')).not.toBeInTheDocument();
  });

  it('formats a stored date in the console language', async () => {
    renderWithProviders(<DateField value="2026-01-15" onChange={() => undefined} />);
    expect(screen.getByRole('button')).toHaveTextContent(displayDate('2026-01-15', 'en'));

    await switchLanguage('zh');

    const localized = displayDate('2026-01-15', 'zh');
    expect(screen.getByRole('button')).toHaveTextContent(localized);
    // The regression this component exists for: an English console on a
    // Chinese system used to show a Chinese date picker.
    expect(localized).toMatch(/年/);
  });

  it('picks the current day from the calendar', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    renderWithProviders(<DateField value="" onChange={onChange} />);

    await user.click(screen.getByRole('button', { name: /Pick a date/ }));
    await user.click(await screen.findByRole('button', { name: i18n.t('ui.today') }));

    expect(onChange).toHaveBeenCalledWith(todayIso());
  });

  it('clears an existing value', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    renderWithProviders(<DateField value="2026-01-15" onChange={onChange} />);

    await user.click(screen.getByRole('button'));
    await user.click(await screen.findByRole('button', { name: i18n.t('ui.clearDate') }));

    expect(onChange).toHaveBeenCalledWith('');
  });

  it('moves the calendar between months', async () => {
    const user = userEvent.setup();
    renderWithProviders(<DateField value="2026-01-15" onChange={() => undefined} />);

    await user.click(screen.getByRole('button'));

    const january = new Intl.DateTimeFormat(i18n.language, {
      month: 'long',
      year: 'numeric',
      timeZone: 'UTC',
    }).format(new Date(Date.UTC(2026, 0, 1)));
    expect(await screen.findByText(january)).toBeInTheDocument();

    await user.click(screen.getByRole('button', { name: i18n.t('ui.nextMonth') }));

    const february = new Intl.DateTimeFormat(i18n.language, {
      month: 'long',
      year: 'numeric',
      timeZone: 'UTC',
    }).format(new Date(Date.UTC(2026, 1, 1)));
    expect(screen.getByText(february)).toBeInTheDocument();
    expect(screen.queryByText(january)).not.toBeInTheDocument();
  });
});
