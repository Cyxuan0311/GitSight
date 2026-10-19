import { cleanup, screen, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { Field, Select } from '@/components/ui/field';
import { renderWithProviders } from '@/test/setup';

afterEach(cleanup);

function BasicSelect(props: Partial<React.ComponentProps<typeof Select>> = {}) {
  return (
    <Select {...props}>
      <option value="">Any</option>
      <option value="github">GitHub</option>
      <option value="gitlab">GitLab</option>
      <option value="codeberg" disabled>
        Codeberg
      </option>
    </Select>
  );
}

describe('Select', () => {
  it('shows the selected value on the trigger', () => {
    renderWithProviders(<BasicSelect value="gitlab" />);
    expect(screen.getByRole('combobox')).toHaveTextContent('GitLab');
  });

  it('shows the placeholder when nothing matches', () => {
    renderWithProviders(<BasicSelect value="" placeholder="Any platform" />);
    expect(screen.getByRole('combobox')).toHaveTextContent('Any platform');
  });

  it('opens a styled listbox rather than a native one', async () => {
    const user = userEvent.setup();
    renderWithProviders(<BasicSelect value="github" />);

    // Nothing is rendered until it is opened, which is what a native select does
    // too -- but this one is ours, so it can be styled.
    expect(screen.queryByRole('listbox')).not.toBeInTheDocument();

    await user.click(screen.getByRole('combobox'));

    const listbox = await screen.findByRole('listbox');
    expect(within(listbox).getAllByRole('option')).toHaveLength(4);
    expect(screen.getByRole('combobox')).toHaveAttribute('aria-expanded', 'true');
  });

  it('reports the chosen value through the native-shaped onChange', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    renderWithProviders(<BasicSelect value="github" onChange={onChange} />);

    await user.click(screen.getByRole('combobox'));
    await user.click(await screen.findByRole('option', { name: 'GitLab' }));

    expect(onChange).toHaveBeenCalledTimes(1);
    // react-hook-form reads exactly this off the event.
    const event = onChange.mock.calls[0]![0];
    expect(event.target).toMatchObject({ name: undefined, value: 'gitlab' });
  });

  it('passes the field name through so register() can record it', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    renderWithProviders(<BasicSelect name="platform" value="github" onChange={onChange} />);

    await user.click(screen.getByRole('combobox'));
    await user.click(await screen.findByRole('option', { name: 'GitLab' }));
    expect(onChange.mock.calls[0]![0].target.name).toBe('platform');
  });

  it('also offers onValueChange', async () => {
    const user = userEvent.setup();
    const onValueChange = vi.fn();
    renderWithProviders(<BasicSelect value="github" onValueChange={onValueChange} />);

    await user.click(screen.getByRole('combobox'));
    await user.click(await screen.findByRole('option', { name: 'GitLab' }));
    expect(onValueChange).toHaveBeenCalledWith('gitlab');
  });

  it('takes part in a native form submission', () => {
    const { container } = renderWithProviders(
      <form data-testid="f">
        <BasicSelect name="platform" value="gitlab" />
      </form>,
    );
    const input = container.querySelector('input[name="platform"]') as HTMLInputElement;
    expect(input).not.toBeNull();
    expect(input.value).toBe('gitlab');
    expect(new FormData(container.querySelector('form')!).get('platform')).toBe('gitlab');
  });

  it('closes without changing when Escape is pressed', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    renderWithProviders(<BasicSelect value="github" onChange={onChange} />);

    await user.click(screen.getByRole('combobox'));
    await screen.findByRole('listbox');
    await user.keyboard('{Escape}');

    expect(screen.queryByRole('listbox')).not.toBeInTheDocument();
    expect(onChange).not.toHaveBeenCalled();
  });

  it('cannot pick a disabled entry', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    renderWithProviders(<BasicSelect value="github" onChange={onChange} />);

    await user.click(screen.getByRole('combobox'));
    const disabled = await screen.findByRole('option', { name: 'Codeberg' });
    expect(disabled).toHaveAttribute('data-disabled');

    await user.click(disabled);
    expect(onChange).not.toHaveBeenCalled();
  });

  it('moves between entries with the arrow keys', async () => {
    const user = userEvent.setup();
    const onChange = vi.fn();
    renderWithProviders(<BasicSelect value="github" onChange={onChange} />);

    await user.click(screen.getByRole('combobox'));
    await screen.findByRole('listbox');
    await user.keyboard('{ArrowDown}{Enter}');

    expect(onChange).toHaveBeenCalledTimes(1);
    expect(onChange.mock.calls[0]![0].target.value).toBeTruthy();
  });

  it('cannot be opened when disabled', async () => {
    const user = userEvent.setup();
    renderWithProviders(<BasicSelect value="github" disabled />);
    expect(screen.getByRole('combobox')).toBeDisabled();
    await user.click(screen.getByRole('combobox'));
    expect(screen.queryByRole('listbox')).not.toBeInTheDocument();
  });
});

describe('Field', () => {
  it('labels its control even when the call site passes no id', async () => {
    const user = userEvent.setup();
    renderWithProviders(
      <Field label="Platform">
        <input />
      </Field>,
    );
    // The association is what a screen reader reads and what a label click uses.
    const input = screen.getByLabelText('Platform') as HTMLInputElement;
    await user.click(screen.getByText('Platform'));
    expect(document.activeElement).toBe(input);
  });

  it('describes the control with its hint', () => {
    renderWithProviders(
      <Field label="Proxy" hint="Only for this machine">
        <input />
      </Field>,
    );
    const input = screen.getByLabelText('Proxy');
    const hint = screen.getByText('Only for this machine');
    expect(input.getAttribute('aria-describedby')).toBe(hint.id);
  });

  it('prefers an explicit htmlFor', () => {
    renderWithProviders(
      <Field label="Platform" htmlFor="my-id">
        <input id="my-id" />
      </Field>,
    );
    expect(screen.getByLabelText('Platform')).toHaveAttribute('id', 'my-id');
  });

  it('renders a non-element child untouched', () => {
    renderWithProviders(
      <Field label="Mixed">
        <>
          <input />
          <input />
        </>
      </Field>,
    );
    expect(screen.getAllByRole('textbox')).toHaveLength(2);
  });

  it('labels a Select the same way', () => {
    renderWithProviders(
      <Field label="Language">
        <BasicSelect value="github" />
      </Field>,
    );
    expect(screen.getByLabelText('Language')).toHaveAttribute('role', 'combobox');
  });
});

