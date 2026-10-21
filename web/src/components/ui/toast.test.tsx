import { act, cleanup, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { ToastViewport } from '@/components/ui/toast';
import { toast, useToastStore } from '@/stores/toast';
import { renderWithProviders } from '@/test/setup';

afterEach(() => {
  cleanup();
  useToastStore.setState({ toasts: [] });
});

describe('the message stack', () => {
  it('shows what a write reported', () => {
    renderWithProviders(<ToastViewport />);

    act(() => toast.success('Provider created'));

    expect(screen.getByText('Provider created')).toBeInTheDocument();
  });

  it('tells a failure apart from a success', () => {
    renderWithProviders(<ToastViewport />);

    act(() => toast.error('Name already in use'));

    const message = screen.getByText('Name already in use');
    expect(message).toBeInTheDocument();
    expect(message.closest('div')).toHaveClass('border-danger/50');
  });

  it('can be dismissed by hand', () => {
    renderWithProviders(<ToastViewport />);

    act(() => toast.success('Settings saved'));
    const close = screen.getByRole('button', { name: /close/i });
    act(() => close.click());

    expect(screen.queryByText('Settings saved')).not.toBeInTheDocument();
  });

  it('keeps only the last few messages', () => {
    renderWithProviders(<ToastViewport />);

    act(() => {
      toast.success('one');
      toast.success('two');
      toast.success('three');
      toast.success('four');
      toast.success('five');
    });

    expect(screen.queryByText('one')).not.toBeInTheDocument();
    expect(screen.getByText('five')).toBeInTheDocument();
  });
});
