import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';
import { Pencil, Plus, Trash2 } from 'lucide-react';
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';

import { InlineError, LoadingBlock } from '@/components/ErrorBoundary';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Field, Input, Select } from '@/components/ui/field';
import { ConfirmDialog, Modal } from '@/components/ui/modal';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import { api } from '@/lib/api';
import { toast } from '@/stores/toast';
import type { MetaOptions, Subscription, SubscriptionInput } from '@/lib/types';

/**
 * A fully-populated row. SubscriptionInput leaves every field optional because
 * it is also the request body; the editor always has a concrete value per
 * control, so it works from this instead of guarding every read.
 */
interface Draft {
  repo: string;
  alias: string;
  platform: string;
  days: number;
  format: string;
  branch: string;
}

const EMPTY: Draft = { repo: '', alias: '', platform: '', days: 0, format: '', branch: '' };

function toDraft(input: SubscriptionInput): Draft {
  return {
    repo: input.repo ?? '',
    alias: input.alias ?? '',
    platform: input.platform ?? '',
    days: input.days ?? 0,
    format: input.format ?? '',
    branch: input.branch ?? '',
  };
}

/**
 * The subscriptions array of config.json, edited as a list.
 *
 * Everything else on the settings page is a flat `key -> value` pair that the
 * server applies through config_set(); subscriptions are a list, so each change
 * goes through the dedicated endpoints -- which is also what keeps the CLI's
 * uniqueness and reference-matching rules in one place. Every mutation writes the
 * file, so this is an editor for the stored config rather than a draft the
 * reader has to remember to save.
 */
export function SubscriptionSettings() {
  const { t } = useTranslation();
  const queryClient = useQueryClient();

  const list = useQuery({ queryKey: ['subscriptions'], queryFn: api.listSubscriptions });
  const options = useQuery({ queryKey: ['meta-options'], queryFn: api.options });

  const [adding, setAdding] = useState(false);
  const [editing, setEditing] = useState<Subscription | null>(null);
  const [removing, setRemoving] = useState<Subscription | null>(null);
  const [error, setError] = useState<string | null>(null);

  // Both keys are refetched: the overview and the subscriptions page read the
  // same list and would otherwise show a stale copy.
  const refresh = () => {
    void queryClient.invalidateQueries({ queryKey: ['subscriptions'] });
    void queryClient.invalidateQueries({ queryKey: ['config'] });
  };

  const remove = useMutation({
    mutationFn: (sub: Subscription) =>
      api.removeSubscription(sub.alias || sub.repo),
    onSuccess: (_result, sub) => {
      toast.success(t('subs.removed', { repo: sub.repo }));
      setRemoving(null);
      setError(null);
      refresh();
    },
    onError: (cause) =>
      toast.error(cause instanceof Error ? cause.message : t('errors.generic')),
  });

  const rows = list.data?.subscriptions ?? [];

  return (
    <Card>
      <CardHeader>
        <div className="space-y-0.5">
          <CardTitle>{t('settings.subscriptions')}</CardTitle>
          <p className="text-xs text-muted">{t('settings.subscriptionsHint')}</p>
        </div>
        <Button
          variant="outline"
          size="sm"
          disabled={rows.length >= (list.data?.max ?? 32)}
          onClick={() => setAdding(true)}
        >
          <Plus />
          {t('subs.add')}
        </Button>
      </CardHeader>
      <CardContent className="px-0 pb-0">
        {list.isPending ? (
          <div className="px-4 pb-4">
            <LoadingBlock rows={2} />
          </div>
        ) : list.isError ? (
          <div className="px-4 pb-4">
            <InlineError
              message={list.error instanceof Error ? list.error.message : t('errors.generic')}
            />
          </div>
        ) : rows.length === 0 ? (
          <EmptyState className="py-8" title={t('subs.empty')} />
        ) : (
          <>
            <Table>
              <thead>
                <tr>
                  <Th>{t('subs.repo')}</Th>
                  <Th>{t('subs.alias')}</Th>
                  <Th>{t('settings.platform')}</Th>
                  <Th className="w-20 text-right">{t('settings.defaultDays')}</Th>
                  <Th>{t('settings.format')}</Th>
                  <Th>{t('analyze.branch')}</Th>
                  <Th className="w-24 text-right">{t('app.edit')}</Th>
                </tr>
              </thead>
              <tbody>
                {rows.map((sub) => (
                  <tr key={`${sub.repo}-${sub.index}`} className="hover:bg-raised/60">
                    <Td className="font-mono text-xs">{sub.repo}</Td>
                    <Td className="text-xs">{sub.alias || '—'}</Td>
                    <Td>
                      {/* An empty override means "inherit the global setting",
                          which is what the CLI's watch list shows too. */}
                      <Badge tone={sub.overrides_platform ? 'accent' : 'neutral'}>
                        {sub.platform}
                      </Badge>
                    </Td>
                    <Td className="nums text-right text-xs">
                      {sub.days}
                      {!sub.overrides_days ? (
                        <span className="ml-1 text-muted">*</span>
                      ) : null}
                    </Td>
                    <Td className="font-mono text-xs">{sub.format}</Td>
                    <Td className="font-mono text-xs">{sub.branch || '—'}</Td>
                    <Td className="text-right">
                      <div className="flex justify-end gap-1">
                        <Button
                          variant="ghost"
                          size="icon-sm"
                          aria-label={t('app.edit')}
                          onClick={() => setEditing(sub)}
                        >
                          <Pencil />
                        </Button>
                        <Button
                          variant="ghost"
                          size="icon-sm"
                          aria-label={t('app.delete')}
                          className="text-danger hover:bg-danger/10"
                          onClick={() => setRemoving(sub)}
                        >
                          <Trash2 />
                        </Button>
                      </div>
                    </Td>
                  </tr>
                ))}
              </tbody>
            </Table>
            <p className="border-t border-line px-4 py-2 text-xs text-muted">
              {t('settings.subscriptionsInherited')}
            </p>
          </>
        )}

        {error ? (
          <div className="px-4 pb-4">
            <InlineError message={error} />
          </div>
        ) : null}
        {remove.isError ? (
          <div className="px-4 pb-4">
            <InlineError
              message={
                remove.error instanceof Error ? remove.error.message : t('errors.generic')
              }
            />
          </div>
        ) : null}
      </CardContent>

      <SubscriptionDialog
        open={adding || editing !== null}
        title={editing ? t('app.edit') : t('subs.add')}
        initial={editing ? toDraft(editing) : EMPTY}
        options={options.data}
        existing={rows}
        onClose={() => {
          setAdding(false);
          setEditing(null);
        }}
        onSaved={(repo, created) => {
          toast.success(created ? t('subs.added', { repo }) : t('subs.updated'));
          setAdding(false);
          setEditing(null);
          setError(null);
          refresh();
        }}
      />

      <ConfirmDialog
        open={removing !== null}
        destructive
        title={t('app.delete')}
        message={t('subs.deleteConfirm', { repo: removing?.repo ?? '' })}
        confirmLabel={t('app.delete')}
        onCancel={() => setRemoving(null)}
        onConfirm={() => removing && remove.mutate(removing)}
      />
    </Card>
  );
}

function SubscriptionDialog({
  open,
  title,
  initial,
  options,
  existing,
  onClose,
  onSaved,
}: {
  open: boolean;
  title: string;
  initial: Draft;
  options: MetaOptions | undefined;
  existing: Subscription[];
  onClose: () => void;
  onSaved: (repo: string, created: boolean) => void;
}) {
  const { t } = useTranslation();
  const [form, setForm] = useState<Draft>(initial);
  const [error, setError] = useState<string | null>(null);

  // Re-seed whenever the dialog opens for a different subscription.
  useEffect(() => {
    if (open) {
      setForm(initial);
      setError(null);
    }
  }, [open, initial]);

  const save = useMutation({
    mutationFn: async () => {
      const repo = form.repo.trim();
      const alias = form.alias.trim();
      // The daemon matches a subscription by repo or alias, so the one being
      // replaced must not also be sent as "a new one".
      const replacing =
        initial !== EMPTY &&
        (initial.repo === repo || (alias !== '' && initial.alias === alias));
      if (replacing) {
        await api.updateSubscription(initial.alias || initial.repo, form);
        return { repo, created: false };
      }
      // The two clashes are different mistakes: one repository subscribed
      // twice, and one alias used for two of them.
      if (existing.some((sub) => sub.repo === repo)) {
        throw new Error(t('subs.existingRepo'));
      }
      if (alias !== '' && existing.some((sub) => sub.alias === alias)) {
        throw new Error(t('subs.duplicateAlias'));
      }
      await api.addSubscription(form);
      return { repo, created: true };
    },
    onSuccess: (result) => onSaved(result.repo, result.created),
    onError: (cause) =>
      setError(cause instanceof Error ? cause.message : t('errors.generic')),
  });

  return (
    <Modal
      open={open}
      onClose={onClose}
      title={title}
      className="w-[min(94vw,40rem)]"
      footer={
        <>
          <Button variant="ghost" size="sm" onClick={onClose}>
            {t('app.cancel')}
          </Button>
          <Button
            variant="primary"
            size="sm"
            disabled={save.isPending || !form.repo.trim()}
            onClick={() => save.mutate()}
          >
            {t('app.save')}
          </Button>
        </>
      }
    >
      <div className="space-y-4">
        <div className="grid gap-4 sm:grid-cols-2">
          <Field label={t('subs.repo')}>
            <Input
              value={form.repo}
              placeholder="owner/repo"
              onChange={(e) => setForm((f) => ({ ...f, repo: e.target.value }))}
            />
          </Field>
          <Field label={t('subs.alias')} hint={t('subs.aliasHint')}>
            <Input
              value={form.alias ?? ''}
              onChange={(e) => setForm((f) => ({ ...f, alias: e.target.value }))}
            />
          </Field>
        </div>

        <div className="grid gap-4 sm:grid-cols-3">
          <Field label={t('settings.platform')}>
            <Select
              value={form.platform ?? ''}
              onChange={(e) => setForm((f) => ({ ...f, platform: e.target.value }))}
            >
              <option value="">{t('settings.inherit')}</option>
              {(options?.platforms ?? []).map((p) => (
                <option key={p.id} value={p.id}>
                  {p.id}
                </option>
              ))}
            </Select>
          </Field>

          <Field label={t('settings.defaultDays')} hint={t('settings.daysHint')}>
            <Input
              type="number"
              min={0}
              value={String(form.days ?? 0)}
              onChange={(e) =>
                setForm((f) => ({ ...f, days: Number(e.target.value) || 0 }))
              }
            />
          </Field>

          <Field label={t('settings.format')}>
            <Select
              value={form.format ?? ''}
              onChange={(e) => setForm((f) => ({ ...f, format: e.target.value }))}
            >
              <option value="">{t('settings.inherit')}</option>
              {(options?.formats ?? []).map((f) => (
                <option key={f.id} value={f.id}>
                  {f.id}
                </option>
              ))}
            </Select>
          </Field>
        </div>

        <Field label={t('analyze.branch')} hint={t('settings.branchHint')}>
          <Input
            value={form.branch ?? ''}
            placeholder={t('ui.branchPlaceholder')}
            onChange={(e) => setForm((f) => ({ ...f, branch: e.target.value }))}
          />
        </Field>

        {error ? <InlineError message={error} /> : null}
      </div>
    </Modal>
  );
}