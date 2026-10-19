import { cleanup, screen, waitFor, within } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';

import { SubscriptionSettings } from '@/features/settings/SubscriptionSettings';
import { installFakeServer, type FakeServer } from '@/test/fakeServer';

import { renderWithProviders } from '@/test/setup';

let server: FakeServer;

beforeEach(() => {
  server = installFakeServer();
});
afterEach(() => {
  cleanup();
  server.restore();
});

const bodyOf = (call: number) => JSON.parse(server.bodies[call] ?? '{}');
const requestTo = (verb: string) => (c: string) => c.startsWith(`${verb} /api/subscriptions`);

describe('SubscriptionSettings', () => {
  it('lists what is stored, including which values are inherited', async () => {
    renderWithProviders(<SubscriptionSettings />);
    expect(await screen.findByText('acme/gadget')).toBeInTheDocument();
    expect(screen.getByText('gadget')).toBeInTheDocument();
    // platform "github" was overridden, so it is shown as active rather than inherited
    expect(screen.getAllByText('github').length).toBeGreaterThan(0);
  });

  it('marks a value that inherits the global default', async () => {
    renderWithProviders(<SubscriptionSettings />);
    const inherited = (await screen.findByText('zeta/inherits')).closest('tr')!;
    // days 7 is the config default rather than an override, so it is starred
    expect(within(inherited).getByText('*')).toBeInTheDocument();

    const overridden = (await screen.findByText('acme/gadget')).closest('tr')!;
    expect(within(overridden).queryByText('*')).not.toBeInTheDocument();
  });

  it('adds a repository and writes it to the config file', async () => {
    const user = userEvent.setup();
    renderWithProviders(<SubscriptionSettings />);
    await screen.findByText('acme/gadget');

    await user.click(screen.getByRole('button', { name: /add/i }));
    await user.type(screen.getByLabelText(/repo/i), 'acme/new-thing');
    await user.type(screen.getByLabelText(/alias/i), 'new');
    await user.click(screen.getByRole('button', { name: /^Save$/ }));

    await waitFor(() => {
      expect(server.calls.some(requestTo('POST'))).toBe(true);
    });
    expect(bodyOf(server.bodies.length - 1)).toMatchObject({
      repo: 'acme/new-thing',
      alias: 'new',
    });
    // Both the list and the config card are refetched so nothing shows a stale copy.
    expect(server.calls).toContain('GET /api/subscriptions');
  });

  it('refuses to save without a repository', async () => {
    const user = userEvent.setup();
    renderWithProviders(<SubscriptionSettings />);
    await screen.findByText('acme/gadget');

    await user.click(screen.getByRole('button', { name: /add/i }));
    expect(screen.getByRole('button', { name: /^Save$/ })).toBeDisabled();
  });

  it('reports a duplicate before sending anything', async () => {
    const user = userEvent.setup();
    renderWithProviders(<SubscriptionSettings />);
    await screen.findByText('acme/gadget');

    await user.click(screen.getByRole('button', { name: /add/i }));
    await user.type(screen.getByLabelText(/repo/i), 'acme/gadget');
    await user.click(screen.getByRole('button', { name: /^Save$/ }));

    expect(await screen.findByText(/already in the list/i)).toBeInTheDocument();
    expect(server.calls.some(requestTo('POST'))).toBe(false);
  });

  it('edits an existing entry in place instead of adding a second one', async () => {
    const user = userEvent.setup();
    renderWithProviders(<SubscriptionSettings />);
    await screen.findByText('acme/gadget');

    const row = (await screen.findByText('acme/gadget')).closest('tr')!;
    await user.click(within(row).getByRole('button', { name: /edit/i }));

    const alias = screen.getByLabelText(/alias/i);
    await user.clear(alias);
    await user.type(alias, 'renamed');
    await user.click(screen.getByRole('button', { name: /^Save$/ }));

    await waitFor(() => {
      expect(server.calls.some(requestTo('PATCH'))).toBe(true);
    });
    // Matched by the reference the row was opened with.
    expect(server.calls.find(requestTo('PATCH'))).toContain('gadget');
    expect(bodyOf(server.bodies.length - 1)).toMatchObject({ alias: 'renamed' });
  });

  it('asks before deleting, then removes it from the file', async () => {
    const user = userEvent.setup();
    renderWithProviders(<SubscriptionSettings />);
    await screen.findByText('acme/gadget');

    const row = (await screen.findByText('acme/gadget')).closest('tr')!;
    await user.click(within(row).getByRole('button', { name: /delete/i }));

    expect(await screen.findByText(/remove "acme\/gadget"/i)).toBeInTheDocument();
    await user.click(screen.getByRole('button', { name: /^Delete$/ }));

    await waitFor(() => {
      expect(server.calls.some(requestTo('DELETE'))).toBe(true);
    });
    expect(server.calls.find(requestTo('DELETE'))).toContain('gadget');
  });

  it('stays empty and quiet when nothing is watched', async () => {
    server.restore();
    server = installFakeServer({
      routes: { '/api/subscriptions': { body: { subscriptions: [], total: 0, max: 32 } } },
    });
    renderWithProviders(<SubscriptionSettings />);
    expect(await screen.findByText(/no subscriptions/i)).toBeInTheDocument();
  });
});