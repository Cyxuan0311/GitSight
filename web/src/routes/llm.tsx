import { zodResolver } from '@hookform/resolvers/zod';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { CheckCircle2, Pencil, Plus, Server, Trash2, XCircle } from 'lucide-react';
import { useState } from 'react';
import { useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { z } from 'zod';

import { AppLayout } from '@/components/layout/AppLayout';
import { InlineError, QueryState } from '@/components/ErrorBoundary';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { Field, Input, Select, Textarea } from '@/components/ui/field';
import { ConfirmDialog, Modal } from '@/components/ui/modal';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import { api } from '@/lib/api';
import type { LlmProvider } from '@/lib/types';

const PROTOCOLS = ['openai', 'anthropic', 'gemini'] as const;

const schema = z.object({
  name: z
    .string()
    .trim()
    .min(1)
    .regex(/^[A-Za-z0-9_-]+$/, 'letters, digits, - and _ only'),
  protocol: z.enum(['auto', ...PROTOCOLS]),
  api_base: z.string().trim().min(1),
  api_key: z.string(),
  model: z.string().trim().min(1),
  timeout_secs: z.coerce.number().int().min(0).max(3600),
  max_tokens: z.coerce.number().int().min(0).max(1_000_000),
  temperature: z.coerce.number().min(0).max(2),
  system_prompt: z.string(),
});

type FormValues = z.infer<typeof schema>;

export function LlmPage() {
  const { t } = useTranslation();
  const list = useQuery({ queryKey: ['llms'], queryFn: api.listLlms });

  const [creating, setCreating] = useState(false);
  const [editing, setEditing] = useState<LlmProvider | null>(null);
  const [removing, setRemoving] = useState<LlmProvider | null>(null);

  return (
    <AppLayout
      path="/llm"
      title={t('llm.title')}
      subtitle={t('llm.subtitle')}
      actions={
        <Button variant="primary" size="sm" onClick={() => setCreating(true)}>
          <Plus />
          {t('llm.add')}
        </Button>
      }
    >
      <div className="space-y-4">
        <Card>
          <CardContent className="p-0">
            <QueryState query={list} rows={2}>
              {(list.data?.llms.length ?? 0) === 0 ? (
                <EmptyState
                  icon={<Server />}
                  title={t('llm.empty')}
                  hint={t('llm.emptyHint')}
                  action={
                    <Button variant="outline" size="sm" onClick={() => setCreating(true)}>
                      <Plus />
                      {t('llm.add')}
                    </Button>
                  }
                />
              ) : (
                <Table>
                  <thead>
                    <tr>
                      <Th className="w-36">{t('llm.name')}</Th>
                      <Th className="w-24">{t('llm.protocol')}</Th>
                      <Th>{t('llm.apiBase')}</Th>
                      <Th className="w-40">{t('llm.model')}</Th>
                      <Th className="w-20 text-right">{t('llm.maxTokens')}</Th>
                      <Th className="w-16 text-right">{t('llm.temperature')}</Th>
                      <Th className="w-40 text-right">{t('app.edit')}</Th>
                    </tr>
                  </thead>
                  <tbody>
                    {list.data?.llms.map((provider) => (
                      <tr key={provider.name} className="hover:bg-raised/60">
                        <Td>
                          <span className="flex items-center gap-1.5">
                            <span className="font-medium text-ink">{provider.name}</span>
                            {provider.active ? (
                              <Badge tone="accent" className="font-normal">
                                {t('llm.active')}
                              </Badge>
                            ) : null}
                            {!provider.usable ? (
                              <Badge tone="warn" className="font-normal">
                                !
                              </Badge>
                            ) : null}
                          </span>
                        </Td>
                        <Td>
                          <Badge tone="neutral" className="font-mono font-normal">
                            {provider.protocol}
                          </Badge>
                        </Td>
                        <Td className="truncate font-mono text-xs">{provider.api_base}</Td>
                        <Td className="truncate font-mono text-xs">{provider.model}</Td>
                        <Td className="nums text-right text-xs">{provider.max_tokens || '-'}</Td>
                        <Td className="nums text-right text-xs">{provider.temperature}</Td>
                        <Td>
                          <RowActions
                            provider={provider}
                            onEdit={() => setEditing(provider)}
                            onRemove={() => setRemoving(provider)}
                          />
                        </Td>
                      </tr>
                    ))}
                  </tbody>
                </Table>
              )}
            </QueryState>
          </CardContent>
        </Card>

        <p className="text-xs text-muted">{t('llm.keyHidden')}</p>

        <LlmForm
          open={creating || editing !== null}
          provider={editing}
          onClose={() => {
            setCreating(false);
            setEditing(null);
          }}
        />

        <ConfirmDialog
          open={removing !== null}
          destructive
          title={t('llm.remove')}
          message={t('llm.removeConfirm', { name: removing?.name ?? '' })}
          confirmLabel={t('llm.remove')}
          onCancel={() => setRemoving(null)}
          onConfirm={() => {
            const target = removing;
            setRemoving(null);
            if (target) void api.deleteLlm(target.name).catch(() => undefined);
          }}
        />
      </div>
    </AppLayout>
  );
}

function RowActions({
  provider,
  onEdit,
  onRemove,
}: {
  provider: LlmProvider;
  onEdit: () => void;
  onRemove: () => void;
}) {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const [testState, setTestState] = useState<'idle' | 'testing' | 'ok' | 'fail'>('idle');
  const [detail, setDetail] = useState<string | null>(null);

  const activate = useMutation({
    mutationFn: () => api.activateLlm(provider.name),
    onSuccess: () => void queryClient.invalidateQueries({ queryKey: ['llms'] }),
  });

  const test = async () => {
    setTestState('testing');
    setDetail(null);
    try {
      const result = await api.testLlm(provider.name);
      setTestState(result.ok ? 'ok' : 'fail');
      setDetail(result.ok ? (result.reply ?? '') : (result.error ?? ''));
    } catch (cause) {
      setTestState('fail');
      setDetail(cause instanceof Error ? cause.message : '');
    }
  };

  return (
    <div className="flex items-center justify-end gap-1">
      {testState === 'testing' ? (
        <span className="text-xs text-muted">{t('llm.testing')}</span>
      ) : testState === 'ok' ? (
        <span title={detail ?? undefined} className="text-ok">
          <CheckCircle2 className="size-4" />
        </span>
      ) : testState === 'fail' ? (
        <span title={detail ?? t('llm.testFailed')} className="text-danger">
          <XCircle className="size-4" />
        </span>
      ) : null}

      <Button
        variant="ghost"
        size="sm"
        disabled={!provider.usable || testState === 'testing'}
        onClick={() => void test()}
      >
        {t('llm.test')}
      </Button>

      {!provider.active ? (
        <Button
          variant="ghost"
          size="sm"
          disabled={activate.isPending}
          onClick={() => activate.mutate()}
        >
          {t('llm.activate')}
        </Button>
      ) : null}

      <Button variant="ghost" size="icon-sm" onClick={onEdit} aria-label={t('app.edit')}>
        <Pencil />
      </Button>
      <Button
        variant="ghost"
        size="icon-sm"
        onClick={onRemove}
        aria-label={t('llm.remove')}
        className="text-danger hover:bg-danger/10"
      >
        <Trash2 />
      </Button>
    </div>
  );
}

function LlmForm({
  open,
  provider,
  onClose,
}: {
  open: boolean;
  provider: LlmProvider | null;
  onClose: () => void;
}) {
  const { t } = useTranslation();
  const queryClient = useQueryClient();

  const form = useForm<FormValues>({
    resolver: zodResolver(schema),
    defaultValues: {
      name: '',
      protocol: 'auto',
      api_base: 'https://api.openai.com/v1',
      api_key: '',
      model: 'gpt-4o-mini',
      timeout_secs: 60,
      max_tokens: 1024,
      temperature: 0.3,
      system_prompt: '',
    },
  });

  // Seed on open so a PATCH starts from the stored values rather than defaults.
  const [seeded, setSeeded] = useState<string | null>(null);
  const key = provider?.name ?? (open ? 'new' : null);
  if (open && key !== seeded) {
    setSeeded(key);
    form.reset({
      name: provider?.name ?? '',
      protocol:
        provider && PROTOCOLS.includes(provider.protocol as (typeof PROTOCOLS)[number])
          ? (provider.protocol as (typeof PROTOCOLS)[number])
          : 'auto',
      api_base: provider?.api_base ?? 'https://api.openai.com/v1',
      // Never seed the key: the server only reports whether one is stored.
      api_key: '',
      model: provider?.model ?? 'gpt-4o-mini',
      timeout_secs: provider?.timeout_secs || 60,
      max_tokens: provider?.max_tokens || 1024,
      temperature: provider?.temperature || 0.3,
      system_prompt: '',
    });
  }
  if (!open && seeded !== null) setSeeded(null);

  const mutation = useMutation({
    mutationFn: async (values: FormValues) => {
      const payload: Record<string, unknown> = {
        protocol: values.protocol === 'auto' ? '' : values.protocol,
        api_base: values.api_base,
        model: values.model,
        timeout_secs: values.timeout_secs,
        max_tokens: values.max_tokens,
        temperature: values.temperature,
      };
      // An untouched key field keeps whatever is stored; the server treats an
      // absent key as "leave alone" and an explicit empty string as "clear".
      if (values.api_key !== '') payload.api_key = values.api_key;
      if (values.system_prompt !== '') payload.system_prompt = values.system_prompt;

      if (provider) {
        return api.patchLlm(provider.name, payload);
      }
      return api.upsertLlm({ ...payload, name: values.name });
    },
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ['llms'] });
      void queryClient.invalidateQueries({ queryKey: ['config'] });
      onClose();
    },
  });

  const errors = form.formState.errors;
  const watching = form.watch('api_key');

  return (
    <Modal
      open={open}
      onClose={onClose}
      title={provider ? t('llm.editTitle') : t('llm.add')}
      className="w-[min(94vw,40rem)]"
      footer={
        <>
          <Button variant="ghost" size="sm" onClick={onClose}>
            {t('app.cancel')}
          </Button>
          <Button
            variant="primary"
            size="sm"
            disabled={mutation.isPending}
            onClick={form.handleSubmit((values) => mutation.mutate(values))}
          >
            {mutation.isPending ? t('app.saving') : t('app.save')}
          </Button>
        </>
      }
    >
      <form className="space-y-4" onSubmit={form.handleSubmit((values) => mutation.mutate(values))}>
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label={t('llm.name')} hint={t('llm.nameHint')} error={errors.name?.message}>
            <Input
              autoComplete="off"
              spellCheck={false}
              disabled={provider !== null}
              invalid={Boolean(errors.name)}
              {...form.register('name')}
            />
          </Field>

          <Field label={t('llm.protocol')} hint={t('llm.protocolHint')}>
            <Select {...form.register('protocol')}>
              <option value="auto">auto</option>
              {PROTOCOLS.map((protocol) => (
                <option key={protocol} value={protocol}>
                  {protocol}
                </option>
              ))}
            </Select>
          </Field>
        </div>

        <Field label={t('llm.apiBase')} error={errors.api_base?.message}>
          <Input
            autoComplete="off"
            spellCheck={false}
            placeholder="https://api.openai.com/v1"
            invalid={Boolean(errors.api_base)}
            {...form.register('api_base')}
          />
        </Field>

        <Field
          label={t('llm.apiKey')}
          hint={
            provider?.api_key.set
              ? watching
                ? t('llm.apiKeyReplace')
                : t('llm.apiKeyKeep')
              : t('llm.apiKeyUnset')
          }
        >
          <Input type="password" autoComplete="off" spellCheck={false} {...form.register('api_key')} />
        </Field>

        <div className="grid gap-4 sm:grid-cols-2">
          <Field label={t('llm.model')} error={errors.model?.message}>
            <Input autoComplete="off" spellCheck={false} invalid={Boolean(errors.model)} {...form.register('model')} />
          </Field>

          <Field label={t('llm.timeout')} error={errors.timeout_secs?.message}>
            <Input type="number" min={0} {...form.register('timeout_secs')} />
          </Field>

          <Field label={t('llm.maxTokens')} error={errors.max_tokens?.message}>
            <Input type="number" min={0} {...form.register('max_tokens')} />
          </Field>

          <Field
            label={t('llm.temperature')}
            hint={String(form.watch('temperature'))}
            error={errors.temperature?.message}
          >
            <Input
              type="number"
              min={0}
              max={2}
              step={0.1}
              {...form.register('temperature')}
            />
          </Field>
        </div>

        <Field
          label={t('llm.systemPrompt')}
          hint={`${t('llm.systemPromptChars', { count: (form.watch('system_prompt') ?? '').length })} · ${t('llm.apiKeyKeep')}`}
        >
          <Textarea rows={4} className="font-mono text-xs" {...form.register('system_prompt')} />
        </Field>

        {mutation.isError ? (
          <InlineError
            message={
              mutation.error instanceof Error ? mutation.error.message : t('errors.generic')
            }
          />
        ) : null}
      </form>
    </Modal>
  );
}