import { cleanup, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import i18n from '@/i18n';

import { CompareRepos } from '@/components/report/CompareRepos';
import { loadFixtures } from '@/test/fakeServer';
import { renderWithProviders } from '@/test/setup';

import type { BatchResult, Report } from '@/lib/types';

const fixtures = loadFixtures();
const batch = fixtures.batch as BatchResult;
const [left, right] = batch.reports.map((entry) => entry.report) as [Report, Report];

afterEach(() => {
  cleanup();
});

describe('two repositories side by side', () => {
  it('names each side by its own repository', () => {
    renderWithProviders(<CompareRepos left={left} right={right} />);

    expect(screen.getByText(left.repo)).toBeInTheDocument();
    expect(screen.getByText(right.repo)).toBeInTheDocument();
  });

  it('shows the same metrics the window comparison shows', () => {
    renderWithProviders(<CompareRepos left={left} right={right} />);

    for (const key of ['compare.metric.prs', 'compare.metric.commits', 'compare.metric.issues']) {
      expect(screen.getByText(i18n.t(key))).toBeInTheDocument();
    }
  });

  it('shows what each side is known for', () => {
    renderWithProviders(<CompareRepos left={left} right={right} />);

    expect(screen.getAllByText(i18n.t('compare.topAuthor')).length).toBeGreaterThan(0);
    expect(screen.getAllByText(i18n.t('compare.topContributor')).length).toBeGreaterThan(0);
  });
});
