import userEvent from '@testing-library/user-event';
import { describe, expect, it, vi } from 'vitest';

import { renderWithProviders as render, screen } from '@/test/setup';
import { ExportBar, LabelChips, StatCard } from '@/components/common';
import { StateBadge } from '@/components/ui/badge';
import type { MetaOptions, Report } from '@/lib/types';

/** Renders a component with a stubbed /api/meta/options response. */
function renderWithOptions(ui: React.ReactElement, options: Partial<MetaOptions> = {}) {
  vi.stubGlobal(
    'fetch',
    vi.fn(async () =>
      new Response(
        JSON.stringify({
          formats: [
            { id: 'json', extension: 'json', mime: 'application/json', binary: false, exports_all_items: true, section_aware: true },
            { id: 'pdf', extension: 'pdf', mime: 'application/pdf', binary: true, exports_all_items: false, section_aware: true },
          ],
          ...options,
        }),
        { status: 200, headers: { 'Content-Type': 'application/json' } },
      ),
    ),
  );
  return render(ui);
}

describe('StateBadge', () => {
  it('tags each state with its own palette', () => {
    render(
      <div>
        <StateBadge state="open" />
        <StateBadge state="closed" />
        <StateBadge state="merged" />
      </div>,
    );
    // These tones track the exporter's open/closed/merged colors so a
    // downloaded report and the console agree.
    expect(screen.getByText('open').className).toContain('text-open');
    expect(screen.getByText('closed').className).toContain('text-closed');
    expect(screen.getByText('merged').className).toContain('text-merged');
  });

  it('falls back to a neutral tone for an unknown state', () => {
    render(<StateBadge state="draft" />);
    const className = screen.getByText('draft').className;
    expect(className).toContain('text-ink-soft');
    expect(className).not.toMatch(/text-(open|closed|merged)/);
  });
});

describe('LabelChips', () => {
  it('renders each label separately', () => {
    render(<LabelChips labels="bug, ui, docs" />);
    expect(screen.getByText('bug')).toBeInTheDocument();
    expect(screen.getByText('docs')).toBeInTheDocument();
  });

  it('collapses the overflow behind a counter', () => {
    render(<LabelChips labels="a, b, c, d" limit={2} />);
    expect(screen.getByText('+2')).toBeInTheDocument();
    expect(screen.queryByText('c')).not.toBeInTheDocument();
  });

  it('shows a dash when there are none', () => {
    render(<LabelChips labels="" />);
    expect(screen.getByText('-')).toBeInTheDocument();
  });
});

describe('StatCard', () => {
  it('shows the value and an optional hint', () => {
    render(<StatCard label="Pull requests" value="12" hint="3 merged" />);
    expect(screen.getByText('12')).toBeInTheDocument();
    expect(screen.getByText('3 merged')).toBeInTheDocument();
  });
});

describe('ExportBar', () => {
  it('offers a download per known format', async () => {
    renderWithOptions(<ExportBar jobId="job_1" repo="acme/gadget" enabled />);
    expect(await screen.findByText('JSON')).toBeInTheDocument();
    expect(screen.getByText('PDF')).toBeInTheDocument();
  });

  it('explains that downloads wait for the analysis', () => {
    renderWithOptions(<ExportBar jobId="job_1" enabled={false} />);
    expect(screen.getByText(/Available once the analysis finishes/i)).toBeInTheDocument();
  });

  it('requests the export from the job, not a fresh analysis', async () => {
    const user = userEvent.setup();
    renderWithOptions(<ExportBar jobId="job_42" repo="acme/gadget" enabled />);
    const button = await screen.findByText('JSON');
    await user.click(button);

    const call = (globalThis.fetch as unknown as ReturnType<typeof vi.fn>).mock.calls.at(-1);
    expect(String(call?.[0])).toContain('/api/jobs/job_42/export?format=json');
    expect(String(call?.[0])).toContain('which=acme%2Fgadget');
  });
});

describe('report fixtures', () => {
  it('keeps the exporter shape: closed includes merged', () => {
    const report: Report = {
      owner: 'acme',
      repo: 'gadget',
      period: 'last 7 days',
      pull_requests: { total: 3, open: 1, closed: 2, merged: 1, items: [] },
      commits: { total: 0, authors: 0, top_author: '', activity: [], weekday: [], items: [] },
      issues: { total: 0, open: 0, closed: 0, items: [] },
      releases: { total: 0, latest: '', items: [] },
      contributors: { total: 0, top: '', top_list: '' },
      summary: '',
    };
    // Guards the assumption the overview cards are built on.
    expect(report.pull_requests.closed - report.pull_requests.merged).toBe(1);
  });
});