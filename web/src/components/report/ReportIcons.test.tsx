import { cleanup, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import i18n from '@/i18n';

import { ReportView } from '@/components/report/ReportView';
import { loadFixtures } from '@/test/fakeServer';
import { renderWithProviders } from '@/test/setup';

import type { Report } from '@/lib/types';

const fixtures = loadFixtures();
const report = fixtures.report as Report;

/**
 * A block is identified by its title alone, which makes the reader work out
 * what it holds. The icon says it at a glance, and it gives the eye something
 * to land on while scrolling a long report -- so the report keeps its icons.
 */
function renderReport() {
  return renderWithProviders(
    <ReportView
      report={report}
      jobId="job_1_1"
      platform="github"
      itemLimit={10}
      showSections={{
        show_prs: true,
        show_commits: true,
        show_issues: true,
        show_releases: true,
        show_contributors: true,
        show_summary: true,
        show_top_labels: true,
        show_languages: true,
      }}
    />,
  );
}

/**
 * The icon that sits inside the heading of the block with this title. The
 * headings carry a count after the name ("Commits · 12"), so the match is on
 * the start of the text.
 */
function iconIn(title: string): Element | null {
  const heading = Array.from(document.querySelectorAll('h3')).find((node) =>
    node.textContent?.trim().startsWith(title),
  );
  return heading?.querySelector('svg') ?? null;
}

afterEach(() => {
  cleanup();
});

describe('report blocks', () => {
  it('marks the summary, the charts and the tables with an icon', () => {
    renderReport();

    for (const key of [
      'report.summary',
      'report.activity',
      'report.weekday',
      'report.languages',
      'report.pullRequests',
      'report.commits',
      'report.issues',
      'report.releases',
      'report.contributors',
    ]) {
      const title = i18n.t(key);
      expect(iconIn(title), `${title} has no icon`).not.toBeNull();
    }
  });

  it('gives the icon to the tab that opens the same block', () => {
    renderReport();

    const tab = screen.getByRole('tab', { name: new RegExp(i18n.t('report.pullRequests'), 'i') });
    expect(tab.querySelector('svg')).not.toBeNull();
  });

  it('marks the overview tiles too', () => {
    renderReport();

    // The tiles repeat the section names, so the same marks are used there.
    const tiles = screen.getAllByText(i18n.t('report.pullRequests'), { selector: 'p' });
    expect(tiles.length).toBeGreaterThan(0);
    expect(tiles.some((tile) => tile.parentElement?.querySelector('svg'))).toBe(true);
  });
});
