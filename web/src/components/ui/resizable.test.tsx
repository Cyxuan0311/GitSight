import { act, cleanup, render, screen } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, describe, expect, it } from 'vitest';

import { ResizableSplit } from '@/components/ui/resizable';

afterEach(() => {
  cleanup();
  localStorage.clear();
});

/**
 * jsdom has no PointerEvent, and testing-library's fallback does not carry
 * clientX. MouseEvent supplies it, and the handler only ever reads clientX, so
 * the drag is exercised with that.
 */
function pointer(target: EventTarget, type: string, clientX = 0) {
  const event = new MouseEvent(type, { bubbles: true, cancelable: true, clientX });
  Object.defineProperty(event, 'pointerId', { value: 1 });
  // act() so React flushes the state update and the effect that attaches the
  // drag listeners before the next event is dispatched.
  act(() => {
    target.dispatchEvent(event);
  });
}

function setup(props: Partial<React.ComponentProps<typeof ResizableSplit>> = {}) {
  return (
    <ResizableSplit
      storageKey="gitsight.test.side"
      defaultWidth={300}
      side={<p>FORM</p>}
      {...props}
    >
      <p>REPORT</p>
    </ResizableSplit>
  );
}

describe('ResizableSplit', () => {
  it('starts at the default width', () => {
    const { container } = renderResizable();
    const pane = container.querySelector('[style]');
    expect(pane).toHaveStyle({ width: '300px' });
    expect(screen.getByRole('separator')).toHaveAttribute('aria-valuenow', '300');
  });

  it('renders both panes', () => {
    renderResizable();
    expect(screen.getByText('FORM')).toBeInTheDocument();
    expect(screen.getByText('REPORT')).toBeInTheDocument();
  });

  it('resizes by dragging the divider left and right', () => {
    const { container } = renderResizable();
    const handle = screen.getByRole('separator');
    // jsdom reports a zero-width box, so the divider sits at x = 0.
    handle.getBoundingClientRect = () => ({ left: 0 }) as DOMRect;

    pointer(handle, 'pointerdown', 0);
    pointer(window, 'pointermove', 420);
    pointer(window, 'pointerup', 420);

    expect(handle).toHaveAttribute('aria-valuenow', '420');
    expect(container.querySelector('[style]')).toHaveStyle({ width: '420px' });
  });

  it('clamps the drag to the allowed range', () => {
    renderResizable({ min: 260, max: 400 });
    const handle = screen.getByRole('separator');
    handle.getBoundingClientRect = () => ({ left: 0 }) as DOMRect;

    pointer(handle, 'pointerdown', 0);
    pointer(window, 'pointermove', 5000);
    pointer(window, 'pointerup', 5000);
    expect(handle).toHaveAttribute('aria-valuenow', '400');

    pointer(handle, 'pointerdown', 0);
    pointer(window, 'pointermove', 10);
    pointer(window, 'pointerup', 10);
    expect(handle).toHaveAttribute('aria-valuenow', '260');
  });

  it('stops following the pointer once the drag ends', () => {
    renderResizable();
    const handle = screen.getByRole('separator');
    handle.getBoundingClientRect = () => ({ left: 0 }) as DOMRect;

    pointer(handle, 'pointerdown', 0);
    pointer(window, 'pointerup', 0);
    pointer(window, 'pointermove', 500);

    expect(handle).toHaveAttribute('aria-valuenow', '300');
  });

  it('resizes with the keyboard, so the layout is reachable without a mouse', async () => {
    const user = userEvent.setup();
    renderResizable({ step: 20 });
    const handle = screen.getByRole('separator');

    handle.focus();
    await user.keyboard('{ArrowRight}');
    expect(handle).toHaveAttribute('aria-valuenow', '320');

    await user.keyboard('{ArrowLeft}');
    expect(handle).toHaveAttribute('aria-valuenow', '300');

    await user.keyboard('{End}');
    expect(handle).toHaveAttribute('aria-valuenow', '620');

    await user.keyboard('{Home}');
    expect(handle).toHaveAttribute('aria-valuenow', '260');
  });

  it('reports itself as a vertical separator with a range', () => {
    renderResizable({ min: 200, max: 500 });
    const handle = screen.getByRole('separator');
    expect(handle).toHaveAttribute('aria-orientation', 'vertical');
    expect(handle).toHaveAttribute('aria-valuemin', '200');
    expect(handle).toHaveAttribute('aria-valuemax', '500');
  });

  it('remembers the width for the next visit', async () => {
    const user = userEvent.setup();
    const first = renderResizable({ step: 20 });
    screen.getByRole('separator').focus();
    await user.keyboard('{ArrowRight}');
    expect(localStorage.getItem('gitsight.test.side')).toBe('320');
    first.unmount();

    renderResizable();
    expect(screen.getByRole('separator')).toHaveAttribute('aria-valuenow', '320');
  });

  it('ignores a stored width outside the range', () => {
    localStorage.setItem('gitsight.test.side', '9999');
    renderResizable({ min: 260, max: 620 });
    expect(screen.getByRole('separator')).toHaveAttribute('aria-valuenow', '300');
  });
});

function renderResizable(props: Partial<React.ComponentProps<typeof ResizableSplit>> = {}) {
  return render(setup(props));
}