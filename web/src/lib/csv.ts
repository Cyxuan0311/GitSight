/**
 * Parser for the `csv` exporter's output, used by the export preview.
 *
 * The exporter groups its rows under `# section` comment lines, each followed by
 * a header row and its data. Reading the file as one flat table would lose that
 * grouping, so the preview renders each section separately.
 */
import { splitLines } from './markdown';
import { splitDelimitedRow } from './row';

export interface CsvSection {
  /** The `# heading` the exporter wrote, or an empty string when it wrote none. */
  title: string;
  header: string[];
  rows: string[][];
}

export function parseCsvSections(src: string): CsvSection[] {
  const sections: CsvSection[] = [];
  let current: CsvSection | null = null;

  for (const raw of splitLines(src)) {
    const line = raw.trim();
    if (line === '') continue;

    if (line.startsWith('#')) {
      current = { title: line.replace(/^#+\s*/, ''), header: [], rows: [] };
      sections.push(current);
      continue;
    }

    const cells = splitDelimitedRow(line, ',');
    if (!current) {
      current = { title: '', header: [], rows: [] };
      sections.push(current);
    }
    // The first row of a section is its header; anything after it is data.
    if (current.header.length === 0) current.header = cells;
    else current.rows.push(cells);
  }

  return sections.filter((section) => section.header.length > 0);
}

/** Shortens a cell for a table cell without hiding that it was truncated. */
export function clampCell(value: string, max = 160): string {
  const flat = value.replace(/\s+/g, ' ').trim();
  return flat.length > max ? `${flat.slice(0, max - 1)}…` : flat;
}