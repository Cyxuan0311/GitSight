import {
  memo,
  useCallback,
  useDeferredValue,
  useMemo,
  useRef,
} from 'react';
import type { TextareaHTMLAttributes } from 'react';

import { tokenize, type Lang, type TokenKind } from '@/lib/highlight';
import { cn } from '@/lib/cn';

/**
 * How each token kind reads against the theme tokens. Both light and dark
 * palettes redefine the same variables, so one map serves both themes.
 */
const TOKEN_CLASS: Partial<Record<TokenKind, string>> = {
  string: 'text-open',
  key: 'text-accent',
  number: 'text-warn',
  literal: 'text-merged',
  punct: 'text-muted',
  comment: 'text-muted italic',
  keyword: 'text-accent',
  variable: 'text-merged',
  flag: 'text-closed',
};

/** The fence languages the tokenizer actually knows; everything else stays plain. */
export function langFromFence(info: string | undefined): Lang {
  switch (info) {
    case 'json':
      return 'json';
    case 'bash':
    case 'sh':
    case 'shell':
    case 'zsh':
    case 'fish':
    case 'console':
    case 'terminal':
      return 'shell';
    default:
      return 'text';
  }
}

/**
 * A read-only code block. Plain text renders as one child exactly like a
 * `<pre>` would; otherwise the tokens become coloring spans around the same
 * characters, so the visible text never diverges from the source.
 *
 * Memoized on its string props: this block can sit inside a layout the
 * reader drags around, and thousands of tiny spans must not repaint for a
 * width change that does not concern them.
 */
export const CodeBlock = memo(function CodeBlock({
  text,
  lang,
  className,
  wrap = true,
}: {
  text: string;
  /** Defaults to plain text -- pass a language to color the code. */
  lang?: Lang;
  className?: string;
  /** Whether long lines wrap; tables of data scroll instead. */
  wrap?: boolean;
}) {
  const tokens = useMemo(
    () => (!lang || lang === 'text' ? null : tokenize(text, lang)),
    [lang, text],
  );

  return (
    <pre
      className={cn(
        'font-mono text-xs leading-relaxed text-ink-soft',
        wrap ? 'whitespace-pre-wrap' : 'whitespace-pre',
        className,
      )}
    >
      {tokens
        ? tokens.map((token, index) =>
            token.kind === 'text' ? (
              token.text
            ) : (
              <span key={index} className={TOKEN_CLASS[token.kind]}>
                {token.text}
              </span>
            ),
          )
        : text}
    </pre>
  );
});

/**
 * An editing textarea with a syntax-colored copy rendered underneath it.
 *
 * The textarea's own text is made transparent -- its caret, selection and
 * scroll stay fully native -- while the colored pre runs behind it, synced to
 * the same scroll position. The two must share identical metrics (font, size,
 * leading, padding, border, wrapping) or the characters drift apart; the
 * `break-words` and `scrollbar-gutter` below are what keep a soft-wrapped
 * textarea and a plain `pre` in agreement.
 */
export function CodeTextarea({
  value,
  onChange,
  lang,
  invalid,
  className,
  ...rest
}: {
  value: string;
  onChange: (text: string) => void;
  lang: Lang;
  invalid?: boolean;
  className?: string;
} & Omit<TextareaHTMLAttributes<HTMLTextAreaElement>, 'value' | 'onChange'>) {
  const textRef = useRef<HTMLTextAreaElement>(null);
  const colorRef = useRef<HTMLPreElement>(null);

  // Tokenizing a large file behind every keystroke should not stall typing;
  // React primes the update once the keystroke has landed, then paints.
  const deferred = useDeferredValue(value);
  const tokens = useMemo(
    () => (!lang || lang === 'text' ? null : tokenize(deferred, lang)),
    [lang, deferred],
  );

  // The textarea is the one that scrolls; the colored copy follows.
  const syncScroll = useCallback(() => {
    const textarea = textRef.current;
    const colored = colorRef.current;
    if (!textarea || !colored) return;
    colored.scrollTop = textarea.scrollTop;
    colored.scrollLeft = textarea.scrollLeft;
  }, []);

  return (
    <div className={cn('relative w-full', className)}>
      <pre
        ref={colorRef}
        aria-hidden
        className={cn(
          // Same metrics as the textarea below: border, padding, font, wrap.
          'pointer-events-none absolute inset-0 overflow-hidden break-words',
          'rounded-field border border-transparent px-3 py-2 font-mono text-xs',
          'leading-relaxed whitespace-pre-wrap [scrollbar-gutter:stable]',
        )}
      >
        {tokens
          ? tokens.map((token, index) =>
              token.kind === 'text' ? (
                token.text
              ) : (
                <span key={index} className={TOKEN_CLASS[token.kind]}>
                  {token.text}
                </span>
              ),
            )
          : deferred}
      </pre>
      <textarea
        ref={textRef}
        className={cn(
          'relative w-full rounded-field border border-line-strong bg-transparent px-3 py-2',
          'font-mono text-xs leading-relaxed text-transparent caret-accent',
          'placeholder:text-muted transition-colors focus:border-accent focus:outline-none',
          'disabled:opacity-60 read-only:bg-raised [scrollbar-gutter:stable]',
          'selection:bg-accent/10',
          invalid && 'border-danger',
        )}
        onScroll={syncScroll}
        onChange={(event) => onChange(event.currentTarget.value)}
        value={value}
        {...rest}
      />
    </div>
  );
}
