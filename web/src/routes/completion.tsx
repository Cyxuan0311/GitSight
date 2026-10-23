import { useQuery } from '@tanstack/react-query';
import { Check, Copy, Terminal } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';

import { AppLayout } from '@/components/layout/AppLayout';
import { LoadingBlock, QueryState } from '@/components/ErrorBoundary';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { CodeBlock } from '@/components/ui/code-block';
import { Tabs, Tab, TabsList, TabsPanel } from '@/components/ui/tabs';
import { api } from '@/lib/api';

const SHELLS = ['bash', 'zsh', 'fish'] as const;
type Shell = (typeof SHELLS)[number];

const INSTALL: Record<Shell, { key: string; snippet: string }> = {
  bash: {
    key: 'completion.installBash',
    snippet:
      'gs completion bash > ~/.local/share/bash-completion/completions/gs\n' +
      '# then: source ~/.local/share/bash-completion/completions/gs',
  },
  zsh: {
    key: 'completion.installZsh',
    snippet: 'gs completion zsh > ~/.zfunc/_gs\nfpath=(~/.zfunc $fpath)\nautoload -Uz compinit && compinit',
  },
  fish: {
    key: 'completion.installFish',
    snippet: 'gs completion fish > ~/.config/fish/completions/gs.fish',
  },
};

export function CompletionPage() {
  const { t } = useTranslation();
  const [shell, setShell] = useState<Shell>('bash');

  const script = useQuery({
    queryKey: ['completion', shell],
    queryFn: () => api.completion(shell),
  });

  return (
    <AppLayout path="/completion" title={t('completion.title')} subtitle={t('completion.subtitle')}>
      <div className="space-y-4">
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Terminal className="size-4" />
              {t(INSTALL[shell].key)}
            </CardTitle>
            <CopyButton text={INSTALL[shell].snippet} />
          </CardHeader>
          <CardContent>
            <CodeBlock
              text={INSTALL[shell].snippet}
              lang="shell"
              className="rounded-field bg-raised p-3 scrollbar-thin"
            />
          </CardContent>
        </Card>

        <Card>
          <CardContent className="p-4">
            <Tabs value={shell} onValueChange={(value) => setShell(value as Shell)}>
              <TabsList>
                {SHELLS.map((entry) => (
                  <Tab key={entry} value={entry}>
                    {entry}
                  </Tab>
                ))}
              </TabsList>

              {SHELLS.map((entry) => (
                <TabsPanel key={entry} value={entry}>
                  <QueryState query={script} rows={3}>
                    <CodeBlock
                      text={script.data ?? ''}
                      lang="shell"
                      className="max-h-[32rem] overflow-auto rounded-field bg-raised p-3 scrollbar-thin"
                      wrap={false}
                    />
                  </QueryState>
                </TabsPanel>
              ))}
            </Tabs>

            <p className="mt-3 text-xs text-muted">{t('completion.hint')}</p>
          </CardContent>
        </Card>
      </div>
    </AppLayout>
  );
}

function CopyButton({ text }: { text: string }) {
  const { t } = useTranslation();
  const [copied, setCopied] = useState(false);

  return (
    <button
      type="button"
      onClick={() => {
        void navigator.clipboard.writeText(text).then(
          () => {
            setCopied(true);
            setTimeout(() => setCopied(false), 1500);
          },
          () => undefined,
        );
      }}
      className="inline-flex items-center gap-1.5 rounded-field border border-line-strong px-2.5 py-1 text-xs font-medium text-ink hover:bg-raised"
    >
      {copied ? <Check className="size-3.5 text-ok" /> : <Copy className="size-3.5" />}
      {copied ? t('app.copied') : t('app.copy')}
    </button>
  );
}

export { LoadingBlock };