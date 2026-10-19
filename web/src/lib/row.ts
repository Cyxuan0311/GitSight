/**
 * Delimiter-aware splitting for the row formats the exporters emit: pipe tables
 * in Markdown, comma-separated records in CSV.
 *
 * Both formats quote a value that contains the delimiter and escape a literal
 * one with a backslash, so the two readers differ only in which byte they cut
 * on.
 */

/** Splits one delimited row, honouring double-quoted fields and escapes. */
export function splitDelimitedRow(line: string, delimiter: ',' | '|'): string[] {
  let body = line.trim();
  if (delimiter === '|') {
    if (body.startsWith('|')) body = body.slice(1);
    if (body.endsWith('|') && !body.endsWith('\\|')) body = body.slice(0, -1);
  }

  const cells: string[] = [];
  let cell = '';
  for (let i = 0; i < body.length; i++) {
    const ch = body[i]!;
    if (ch === '\\' && body[i + 1] === delimiter) {
      cell += delimiter;
      i++;
      continue;
    }
    if (ch === '"') {
      i++;
      while (i < body.length) {
        if (body[i] === '"' && body[i + 1] === '"') {
          cell += '"';
          i++;
        } else if (body[i] === '"') {
          break;
        } else {
          cell += body[i]!;
        }
        i++;
      }
      continue;
    }
    if (ch === delimiter) {
      cells.push(cell);
      cell = '';
      continue;
    }
    cell += ch;
  }
  cells.push(cell);
  return cells.map((entry) => entry.trim());
}