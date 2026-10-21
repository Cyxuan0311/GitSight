import { cleanup, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import i18n from '@/i18n';

import { CompareDetails } from '@/components/report/CompareDetails';
import { loadFixtures } from '@/test/fakeServer';
import { renderWithProviders } from '@/test/setup';

import type { CompareResult } from '@/lib/types';

const fixtures = loadFixtures();
const result = fixtures.compare as CompareResult;

/**
 * The fixture was captured with timestamps relative to the day it was taken,
 * and the window is measured from today, so the clock is pinned to the
 * capture: otherwise the test would start failing a week after the capture.
 */
const CAPTURED = Date.parse(fixtures.capturedAt);

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

describe('the item-level view of a comparison', () => {
  it('lists what changed inside the shorter window', () => {
    vi.spyOn(Date, 'now').mockReturnValue(CAPTURED);
    renderWithProviders(<CompareDetails result={result} />);

    expect(screen.getByText(i18n.t('compare.createdPrs'))).toBeInTheDocument();
    expect(screen.getByText(i18n.t('compare.mergedPrs'))).toBeInTheDocument();
    expect(screen.getByText(i18n.t('compare.closedIssues'))).toBeInTheDocument();
    // The pull request the fixture carries, by number and by title.
    expect(screen.getAllByText('#42').length).toBeGreaterThan(0);
    expect(screen.getAllByText('Fix the widget').length).toBeGreaterThan(0);
  });

  it('links each item back to the platform', () => {
    vi.spyOn(Date, 'now').mockReturnValue(CAPTURED);
    renderWithProviders(<CompareDetails result={result} />);

    const link = screen.getAllByRole('link', { name: '#42' })[0];
    expect(link).toHaveAttribute('href', 'https://example.test/p/42');
  });

  it('reports the difference between the two windows', () => {
    vi.spyOn(Date, 'now').mockReturnValue(CAPTURED);
    renderWithProviders(<CompareDetails result={result} />);

    expect(screen.getByText(i18n.t('compare.difference'))).toBeInTheDocument();
  });
});
