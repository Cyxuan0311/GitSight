import { zodResolver } from '@hookform/resolvers/zod';
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { useNavigate } from '@tanstack/react-router';
import { Pencil, Play, Plus, Trash2, Users } from 'lucide-react';
import { useState } from 'react';
import { Controller, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { z } from 'zod';

import { AppLayout } from '@/components/layout/AppLayout';
import { InlineError, QueryState } from '@/components/ErrorBoundary';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { Field, Input, Select } from '@/components/ui/field';
import { ConfirmDialog, Modal } from '@/components/ui/modal';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import { api } from '@/lib/api';
import { toast } from '@/stores/toast';
import type { Subscription } from '@/lib/types';

import { SubscriptionBatchPanel } from '@/features/subscriptions/SubscriptionBatchPanel';

const schema = z.object({
  repo: z.string().trim().min(1),
  alias: z.string().trim(),
  platform: z.string(),
  days: z.string(),
  format: z.string(),
  branch: z.string().trim(),
});

type FormValues = z.infer<typeof schema>;

export function SubscriptionsPage() {
  const { t } = useTranslation();
  const list = useQuery({ queryKey: ['subscriptions'], queryFn: api.listSubscriptions });

  const [editing, setEditing] = useState<Subscription | null>(null);
  const [creating, setCreating] = useState(false);
  const [removing, setRemoving] = useState<Subscription | null>(null);

  const subscriptions = list.data?.subscriptions ?? [];

  return (
    <AppLayout
      path="/subscriptions"
      title={t('subs.title')}
      subtitle={t('subs.subtitle')}
      actions={
        <Button variant="primary" size="sm" onClick={() => setCreating(true)}>
          <Plus />
          {t('subs.add')}
        </Button>
      }
    >
      <div className="space-y-4">
        <SubscriptionBatchPanel />

        <Card>
          <CardContent className="p-0">
            <QueryState query={list} rows={2}>
              {subscriptions.length === 0 ? (
                <EmptyState
                  icon={<Users />}
                  title={t('subs.empty')}
                  hint={t('subs.emptyHint')}
                  action={
                    <Button variant="outline" size="sm" onClick={() => setCreating(true)}>
                      <Plus />
                      {t('subs.add')}
                    </Button>
                  }
                />
              ) : (
                <Table>
                  <thead>
                    <tr>
                      <Th>{t('subs.repo')}</Th>
                      <Th className="w-28">{t('subs.alias')}</Th>
                      <Th className="w-24">{t('subs.platform')}</Th>
                      <Th className="w-20 text-right">{t('subs.days')}</Th>
                      <Th className="w-20">{t('subs.format')}</Th>
                      <Th className="w-28">{t('subs.branch')}</Th>
                      <Th className="w-32 text-right">{t('app.edit')}</Th>
                    </tr>
                  </thead>
                  <tbody>
                    {subscriptions.map((entry) => (
                      <tr key={entry.repo} className="hover:bg-raised/60">
                        <Td className="font-mono text-xs text-ink">{entry.repo}</Td>
                        <Td>
                          {entry.alias ? (
                            <Badge tone="accent">{entry.alias}</Badge>
                          ) : (
                            <span className="text-xs text-muted">-</span>
                          )}
                        </Td>
                        <Td>
                          <Override value={entry.platform} over={entry.overrides_platform} />
                        </Td>
                        <Td className="nums text-right">
                          <Override value={String(entry.days)} over={entry.overrides_days} />
                        </Td>
                        <Td>
                          <Override value={entry.format} over={entry.overrides_format} />
                        </Td>
                        <Td className="font-mono text-xs">
                          <Override
                            value={entry.branch || '-'}
                            over={entry.overrides_branch}
                          />
                        </Td>
                        <Td>
                          <div className="flex justify-end gap-1">
                            <AnalyzeButton entry={entry} />
                            <Button
                              variant="ghost"
                              size="icon-sm"
                              onClick={() => setEditing(entry)}
                              aria-label={t('app.edit')}
                            >
                              <Pencil />
                            </Button>
                            <Button
                              variant="ghost"
                              size="icon-sm"
                              onClick={() => setRemoving(entry)}
                              aria-label={t('app.delete')}
                              className="text-danger hover:bg-danger/10"
                            >
                              <Trash2 />
                            </Button>
                          </div>
                        </Td>
                      </tr>
                    ))}
                  </tbody>
                </Table>
              )}
            </QueryState>
          </CardContent>
        </Card>

        <SubscriptionForm
          open={creating || editing !== null}
          entry={editing}
          onClose={() => {
            setCreating(false);
            setEditing(null);
          }}
        />

        <ConfirmDialog
          open={removing !== null}
          destructive
          title={t('app.delete')}
          message={t('subs.removeConfirm', { repo: removing?.repo ?? '' })}
          confirmLabel={t('app.delete')}
          onCancel={() => setRemoving(null)}
          onConfirm={() => {
            const target = removing;
            setRemoving(null);
            if (target) {
              void api
                .removeSubscription(target.alias || target.repo)
                .then(() => toast.success(t('subs.removed', { repo: target.repo })))
                .catch((cause: unknown) =>
                  toast.error(cause instanceof Error ? cause.message : t('errors.generic')),
                );
            }
          }}
        />
      </div>
    </AppLayout>
  );
}

/** Marks a value that came from the subscription rather than the global config. */
function Override({ value, over }: { value: string; over: boolean }) {
  const { t } = useTranslation();
  if (!over) {
    return <span className="text-xs text-muted" title={t('subs.inherited')}>{value}</span>;
  }
  return (
    <Badge tone="accent" className="font-normal" title={t('subs.overridden')}>
      {value}
    </Badge>
  );
}

function AnalyzeButton({ entry }: { entry: Subscription }) {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const run = async () => {
    setBusy(true);
    setError(null);
    try {
      const job = await api.analyzeSubscription(entry.alias || entry.repo);
      // The analyze page owns the run and adopts the id from ?job=, so the
      // progress stream and the finished report both land there.
      await navigate({ to: '/analyze', search: { job: job.id } });
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : null);
    } finally {
      setBusy(false);
      void queryClient.invalidateQueries({ queryKey: ['jobs'] });
    }
  };

  return (
    <>
      <Button variant="ghost" size="icon-sm" onClick={() => void run()} disabled={busy} aria-label={t('subs.analyze')}>
        <Play />
      </Button>
      {error ? <span className="sr-only">{error}</span> : null}
    </>
  );
}

function SubscriptionForm({
  open,
  entry,
  onClose,
}: {
  open: boolean;
  entry: Subscription | null;
  onClose: () => void;
}) {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const { data: options } = useQuery({ queryKey: ['meta-options'], queryFn: api.options });
  const { data: config } = useQuery({ queryKey: ['config'], queryFn: api.getConfig });

  const form = useForm<FormValues>({
    resolver: zodResolver(schema),
    defaultValues: {
      repo: '',
      alias: '',
      platform: '',
      days: '',
      format: '',
      branch: '',
    },
  });

  // Re-seed the form each time the dialog opens with a different target.
  const [seeded, setSeeded] = useState<string | null>(null);
  const key = entry?.repo ?? (open ? 'new' : null);
  if (open && key !== seeded) {
    setSeeded(key);
    form.reset({
      repo: entry?.repo ?? '',
      alias: entry?.alias ?? '',
      platform: entry?.platform ?? '',
      days: entry ? String(entry.days) : '',
      format: entry?.format ?? '',
      branch: entry?.branch ?? '',
    });
  }
  if (!open && seeded !== null) setSeeded(null);

  const mutation = useMutation({
    mutationFn: async (values: FormValues) => {
      // Empty means "inherit", which the server stores by clearing the override.
      const days = values.days.trim() === '' ? null : Number(values.days);
      const payload = {
        repo: values.repo.trim(),
        alias: values.alias.trim() || undefined,
        platform: values.platform || null,
        days: Number.isFinite(days) && days !== null && days > 0 ? days : null,
        format: values.format || null,
        branch: values.branch.trim() || null,
      };
      if (entry) {
        return api.updateSubscription(entry.alias || entry.repo, payload);
      }
      return api.addSubscription(payload);
    },
    onSuccess: () => {
      void queryClient.invalidateQueries({ queryKey: ['subscriptions'] });
      void queryClient.invalidateQueries({ queryKey: ['config'] });
      onClose();
    },
  });

  const errors = form.formState.errors;

  return (
    <Modal
      open={open}
      onClose={onClose}
      title={entry ? t('subs.editTitle') : t('subs.add')}
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
      <form
        className="space-y-4"
        onSubmit={form.handleSubmit((values) => mutation.mutate(values))}
      >
        <Field label={t('subs.repo')} error={errors.repo?.message}>
          <Input
            autoComplete="off"
            spellCheck={false}
            placeholder="curl/curl"
            invalid={Boolean(errors.repo)}
            {...form.register('repo')}
          />
        </Field>

        <Field label={t('subs.alias')} hint={t('subs.aliasHint')}>
          <Input autoComplete="off" spellCheck={false} {...form.register('alias')} />
        </Field>

        <div className="grid gap-4 sm:grid-cols-2">
          <Field label={t('subs.platform')} hint={t('subs.inherit')}>
            <Controller
              control={form.control}
              name="platform"
              render={({ field }) => (
                <Select value={field.value} onChange={field.onChange}>
                  <option value="">{t('subs.inherit')}</option>
                  {(options?.platforms ?? []).map((platform) => (
                    <option key={platform.id} value={platform.id}>
                      {platform.id}
                    </option>
                  ))}
                </Select>
              )}
            />
          </Field>

          <Field label={t('subs.days')} hint={t('subs.inherit')}>
            <Controller
              control={form.control}
              name="days"
              render={({ field }) => (
                <Input
                  type="number"
                  min={1}
                  placeholder={String(config?.default_days ?? 7)}
                  value={field.value}
                  onChange={field.onChange}
                />
              )}
            />
          </Field>

          <Field label={t('subs.format')} hint={t('subs.inherit')}>
            <Controller
              control={form.control}
              name="format"
              render={({ field }) => (
                <Select value={field.value} onChange={field.onChange}>
                  <option value="">{t('subs.inherit')}</option>
                  {(options?.formats ?? []).map((format) => (
                    <option key={format.id} value={format.id}>
                      {format.id}
                    </option>
                  ))}
                </Select>
              )}
            />
          </Field>

          <Field label={t('subs.branch')} hint={t('subs.inherit')}>
            <Input placeholder={t('ui.branchPlaceholder')} autoComplete="off" spellCheck={false} {...form.register('branch')} />
          </Field>
        </div>

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