import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { CodeBlock, CodeTextarea, langFromFence } from '@/components/ui/code-block';
import { useState } from 'react';

afterEach(cleanup);

describe('CodeBlock', () => {
  it('wraps the same text in coloring spans, without dropping any character', () => {
    render(<CodeBlock text={'{"name": "gs"}'} lang="json" />);
    expect(screen.getByText('"name"').className).toBe('text-accent');
    // The pre's full text is the source, character for character.
    expect(document.querySelector('pre')?.textContent).toBe('{"name": "gs"}');
  });

  it('renders plain text with no spans at all', () => {
    render(<CodeBlock text={'echo "$HOME"'} lang="text" />);
    expect(document.querySelector('span')).toBeNull();
    expect(document.querySelector('pre')?.textContent).toBe('echo "$HOME"');
  });

  it('maps the fence languages a viewer can color', () => {
    expect(langFromFence('bash')).toBe('shell');
    expect(langFromFence('console')).toBe('shell');
    expect(langFromFence('json')).toBe('json');
    expect(langFromFence('toml')).toBe('text');
    expect(langFromFence(undefined)).toBe('text');
  });
});

/** The editor keeps its own state, like the settings dialog does. */
function EditorHarness() {
  const [value, setValue] = useState('{"days": 3}');
  return (
    <CodeTextarea
      value={value}
      onChange={setValue}
      lang="json"
      aria-label="raw config"
    />
  );
}

describe('CodeTextarea', () => {
  it('keeps a real textarea for input and paints the colors underneath', () => {
    render(<EditorHarness />);
    const textarea = screen.getByLabelText('raw config');
    expect(textarea.tagName).toBe('TEXTAREA');
    expect(textarea).toHaveValue('{"days": 3}');
    // The colored copy is aria-hidden; the textarea is the accessible field.
    expect(screen.getByText('"days"').parentElement).toHaveAttribute('aria-hidden');
  });

  it('colors what the reader has typed', () => {
    render(<EditorHarness />);
    fireEvent.change(screen.getByLabelText('raw config'), {
      target: { value: '{"line": "new"}' },
    });
    const colored = document.querySelector('pre')?.textContent;
    expect(colored).toBe('{"line": "new"}');
    expect(screen.getByText('"line"')).toHaveClass('text-accent');
  });

  it('the textarea scrolls and the colored copy follows', () => {
    render(<CodeTextarea value={'a\nb\nc'} onChange={() => {}} lang="text" />);
    const textarea = document.querySelector('textarea') as HTMLTextAreaElement;
    const colored = document.querySelector('pre') as HTMLPreElement;
    textarea.scrollTop = 24;
    fireEvent.scroll(textarea);
    expect(colored.scrollTop).toBe(24);
  });
});
