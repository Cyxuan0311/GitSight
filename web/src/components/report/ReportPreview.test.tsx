import { cleanup, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { ReportPreview } from '@/components/report/ReportPreview';
import { formatBytes } from '@/lib/format';
import { renderWithProviders } from '@/test/setup';
import { installFakeServer, loadFixtures, type FakeServer } from '@/test/fakeServer';

const fixtures = loadFixtures();
const FORMATS = (fixtures.options as { formats: { id: string }[] }).formats.map((f) => f.id);

const ARTIFACTS: Record<string, string> = {
  md: '# GitSight Report\n\n**Repository**: acme/gadget\n\n| a | b |\n|---|---|\n| 1 | 2 |\n',
  txt: 'GitSight Report\nRepository: acme/gadget\n',
  csv: '# metrics\nkey,value\nrepository,acme\nstars,1234\n\n# prs\nnumber,title\n42,Fix\n',
  json: '{"owner":"acme","repo":"gadget"}',
  html: '<!doctype html><title>GitSight</title><p>acme/gadget</p>',
};

/** Answers /api/jobs/:id/export with a body per format, like the daemon does. */
function stubExports(calls: string[]) {
  return (input: RequestInfo | URL) => {
    const url = String(input);
    calls.push(url);
    const format = new URL(url, 'http://localhost').searchParams.get('format') ?? '';
    if (format === 'pdf') {
      return new Response(new Uint8Array([0x25, 0x50, 0x44, 0x46]), {
        status: 200,
        headers: {
          'Content-Type': 'application/pdf',
          'Content-Disposition': 'attachment; filename="acme_gadget.pdf"',
        },
      });
    }
    if (format === 'xlsx') {
      return new Response(new Uint8Array([0x50, 0x4b, 3, 4]), {
        status: 200,
        headers: {
          'Content-Type': 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
          'Content-Disposition': 'attachment; filename="acme_gadget.xlsx"',
        },
      });
    }
    return new Response(ARTIFACTS[format] ?? '', {
      status: 200,
      headers: {
        'Content-Type': 'text/plain; charset=utf-8',
        'Content-Disposition': `attachment; filename="acme_gadget.${format}"`,
      },
    });
  };
}

let server: FakeServer;
let calls: string[];

beforeEach(() => {
  calls = [];
  server = installFakeServer();
  globalThis.fetch = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
    if (String(input).includes('/export')) return Promise.resolve(stubExports(calls)(input));
    return server.fetch(input, init);
  }) as typeof fetch;
});

afterEach(() => {
  cleanup();
  server.restore();
  // jsdom implements neither, and the PDF pane needs both.
  Object.defineProperty(URL, 'createObjectURL', {
    writable: true,
    configurable: true,
    value: vi.fn(() => 'blob:gitsight-preview'),
  });
  Object.defineProperty(URL, 'revokeObjectURL', {
    writable: true,
    configurable: true,
    value: vi.fn(),
  });
});

function renderPreview() {
  return renderWithProviders(
    <ReportPreview jobId="job_1_1" repo="acme/gadget" options={fixtures.options as never} report={<p>LIVE REPORT BODY</p>} />,
  );
}

async function switchTo(user: ReturnType<typeof userEvent.setup>, name: string) {
  await user.click(screen.getByRole('tab', { name: new RegExp(name, 'i') }));
}

describe('ReportPreview', () => {
  it('offers a tab per format the daemon renders, plus the live report', async () => {
    renderPreview();
    for (const id of ['report', ...FORMATS]) {
      const label = id === 'report' ? 'Report' : id.toUpperCase();
      expect(await screen.findByRole('tab', { name: new RegExp(label) })).toBeInTheDocument();
    }
  });

  it('shows the console rendering first, with no export requested', () => {
    renderPreview();
    expect(screen.getByText('LIVE REPORT BODY')).toBeInTheDocument();
    expect(calls).toEqual([]);
  });

  it('renders the markdown export as a document, not as raw syntax', async () => {
    const user = userEvent.setup();
    renderPreview();
    await switchTo(user, 'MD');

    expect(await screen.findByText('GitSight Report')).toBeInTheDocument();
    // The heading, not the literal "# " prefix.
    expect(screen.queryByText('# GitSight Report')).not.toBeInTheDocument();
    expect(screen.getByText('Repository')).toBeInTheDocument();
    // The table becomes a real table.
    const cell = await screen.findByText('2');
    expect(cell.tagName).toMatch(/TD|TH/);
  });

  it('renders the csv export as one table per section', async () => {
    const user = userEvent.setup();
    renderPreview();
    await switchTo(user, 'CSV');

    expect(await screen.findByText('metrics')).toBeInTheDocument();
    expect(screen.getByText('prs')).toBeInTheDocument();
    expect(screen.getByText('acme')).toBeInTheDocument();
  });

  it('pretty-prints the json export', async () => {
    const user = userEvent.setup();
    renderPreview();
    await switchTo(user, 'JSON');

    // The pretty JSON sits in a colored code block whose line is split across
    // coloring spans, so the pane's whole text is what carries the assertion.
    const pre = await screen.findByText((_, element) =>
      element?.tagName === 'PRE' && (element?.textContent ?? '').includes('"owner": "acme"'),
    );
    expect(pre).toBeInTheDocument();
  });

  it('shows the text export verbatim', async () => {
    const user = userEvent.setup();
    renderPreview();
    await switchTo(user, 'TXT');
    expect(await screen.findByText(/Repository: acme\/gadget/)).toBeInTheDocument();
  });

  it('isolates the html export in a sandboxed frame', async () => {
    const user = userEvent.setup();
    renderPreview();
    await switchTo(user, 'HTML');

    const frame = await screen.findByTitle('acme_gadget.html');
    expect(frame.tagName).toBe('IFRAME');
    expect(frame.getAttribute('sandbox')).toBe('allow-scripts');
    expect(frame.getAttribute('srcdoc')).toContain('acme/gadget');
  });

  it('renders the pdf export in a frame fed by an object url', async () => {
    const user = userEvent.setup();
    renderPreview();
    await switchTo(user, 'PDF');

    const frame = await screen.findByTitle('acme_gadget.pdf');
    expect(frame.tagName).toBe('IFRAME');
    expect(frame.getAttribute('src')).toBe('blob:gitsight-preview');
    expect(calls.some((url) => url.includes('format=pdf'))).toBe(true);
  });

  it('describes a spreadsheet instead of claiming the export is empty', async () => {
    const user = userEvent.setup();
    renderPreview();
    await switchTo(user, 'XLSX');

    // A spreadsheet has no text to show, but it is a real 4-byte xlsx -- calling
    // it empty was wrong and read as a broken export.
    expect(await screen.findByText(/Spreadsheets have no browser renderer/i)).toBeInTheDocument();
    expect(screen.getByText(/Open it in a spreadsheet application/i)).toBeInTheDocument();
    expect(screen.queryByText(/export is empty/i)).not.toBeInTheDocument();
    expect(screen.getByText('acme_gadget.xlsx')).toBeInTheDocument();
    expect(
      screen.getByText(
        'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet',
      ),
    ).toBeInTheDocument();
    // The size appears in the card header and again in the body.
    expect(screen.getAllByText('4 B').length).toBeGreaterThan(0);
    expect(screen.getByRole('button', { name: /^Download$/ })).toBeInTheDocument();
  });

  it('re-renders the artifacts when the report language changes', async () => {
    const user = userEvent.setup();
    const { rerender } = renderWithProviders(
      <ReportPreview
        jobId="job_1_1"
        repo="acme/gadget"
        lang="en"
        options={fixtures.options as never}
        report={<p>LIVE</p>}
      />,
    );
    await switchTo(user, 'MD');
    await screen.findByText('GitSight Report');
    expect(calls.at(-1)).toContain('lang=en');

    rerender(
      <ReportPreview
        jobId="job_1_1"
        repo="acme/gadget"
        lang="zh"
        options={fixtures.options as never}
        report={<p>LIVE</p>}
      />,
    );
    // The cached artifact was in the previous language, so it must be refetched.
    await waitFor(() => expect(calls.at(-1)).toContain('lang=zh'));
  });

  it('centres the waiting state and reserves its height so the tab does not jump', async () => {
    const user = userEvent.setup();
    let release: (() => void) | undefined;
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });

    // The artifact request is held open so the pending state can be inspected.
    globalThis.fetch = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      if (String(input).includes('/export')) {
        return gate.then(() => stubExports([])(input));
      }
      return server.fetch(input, init);
    }) as typeof fetch;

    renderPreview();
    await switchTo(user, 'PDF');

    // The spinner also carries role=status, so target the pane directly.
    const pane = await waitFor(() => {
      const node = document.querySelector('[aria-busy].place-items-center');
      if (!node) throw new Error('the centred waiting state never appeared');
      return node as HTMLElement;
    });
    expect(pane.className).toContain('place-items-center');
    // A reserved height is what stops the page jumping as the tab swaps content.
    expect(pane.style.minHeight).toBe('420px');

    release?.();
    await screen.findByTitle('acme_gadget.pdf');
  });

  it('requests each artifact once and reuses it when switching back', async () => {
    const user = userEvent.setup();
    renderPreview();

    await switchTo(user, 'MD');
    await screen.findByText('GitSight Report');
    await switchTo(user, 'CSV');
    await screen.findByText('metrics');
    await switchTo(user, 'MD');

    expect(await screen.findByText('GitSight Report')).toBeInTheDocument();
    const mdRequests = calls.filter((url) => url.includes('format=md'));
    expect(mdRequests).toHaveLength(1);
  });

  it('identifies the report with the pair the daemon resolves', async () => {
    const user = userEvent.setup();
    renderPreview();
    await switchTo(user, 'MD');
    await screen.findByText('GitSight Report');
    expect(calls.at(-1)).toContain('which=acme%2Fgadget');
  });

  it('reports a failed render and offers a retry', async () => {
    const user = userEvent.setup();
    globalThis.fetch = vi.fn((input: RequestInfo | URL, init?: RequestInit) => {
      if (String(input).includes('/export')) {
        return Promise.resolve(
          new Response(JSON.stringify({ error: 'not_ready', message: 'the job is still running', status: 409 }), {
            status: 409,
            headers: { 'Content-Type': 'application/json' },
          }),
        );
      }
      return server.fetch(input, init);
    }) as typeof fetch;

    renderPreview();
    await switchTo(user, 'MD');
    expect(await screen.findByText(/still running/i)).toBeInTheDocument();
    expect(screen.getByRole('button', { name: /retry/i })).toBeInTheDocument();
  });

  it('downloads the artifact that is on screen', async () => {
    const user = userEvent.setup();
    const clicked: string[] = [];
    vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(function (this: HTMLAnchorElement) {
      clicked.push(this.download);
    });

    renderPreview();
    await switchTo(user, 'CSV');
    await screen.findByText('metrics');
    await user.click(screen.getByRole('button', { name: /^Download$/ }));

    await waitFor(() => expect(clicked).toEqual(['acme_gadget.csv']));
    expect(calls.some((url) => url.includes('format=csv'))).toBe(true);
  });
});

describe('formatBytes', () => {
  it('scales the unit', () => {
    expect(formatBytes(512)).toBe('512 B');
    expect(formatBytes(2048)).toBe('2.0 KB');
    expect(formatBytes(3 * 1024 * 1024)).toBe('3.0 MB');
  });
});