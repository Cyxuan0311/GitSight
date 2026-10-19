import { Dialog as BaseDialog } from '@base-ui/react/dialog';
import { X } from 'lucide-react';
import { useTranslation } from 'react-i18next';

import { cn } from '@/lib/cn';

interface ModalProps {
  open: boolean;
  onClose: () => void;
  title: string;
  description?: string;
  children: React.ReactNode;
  footer?: React.ReactNode;
  className?: string;
}

export function Modal({ open, onClose, title, description, children, footer, className }: ModalProps) {
  const { t } = useTranslation();
  return (
    <BaseDialog.Root open={open} onOpenChange={(next) => (next ? undefined : onClose())}>
      <BaseDialog.Portal>
        <BaseDialog.Backdrop
          className={cn(
            'fixed inset-0 z-40 bg-black/45 backdrop-blur-[2px]',
            'transition-opacity duration-150 data-[ending-style]:opacity-0',
          )}
        />
        <BaseDialog.Popup
          className={cn(
            'fixed top-1/2 left-1/2 z-50 w-[min(92vw,32rem)] -translate-x-1/2 -translate-y-1/2',
            'rounded-card border border-line bg-surface shadow-xl',
            'transition-transform duration-150 data-[ending-style]:scale-95 data-[ending-style]:opacity-0',
            className,
          )}
        >
          <div className="flex items-start justify-between gap-4 border-b border-line p-4">
            <div className="space-y-1">
              <BaseDialog.Title className="text-sm font-semibold text-ink">
                {title}
              </BaseDialog.Title>
              {description ? (
                <BaseDialog.Description className="text-xs text-muted">
                  {description}
                </BaseDialog.Description>
              ) : null}
            </div>
            <BaseDialog.Close
              aria-label={t('app.close')}
              className="-mr-1 -mt-1 rounded p-1 text-muted hover:bg-raised hover:text-ink"
            >
              <X className="size-4" />
            </BaseDialog.Close>
          </div>
          <div className="max-h-[70vh] overflow-y-auto p-4 scrollbar-thin">{children}</div>
          {footer ? (
            <div className="flex items-center justify-end gap-2 border-t border-line p-4">
              {footer}
            </div>
          ) : null}
        </BaseDialog.Popup>
      </BaseDialog.Portal>
    </BaseDialog.Root>
  );
}

interface ConfirmProps {
  open: boolean;
  title: string;
  message: React.ReactNode;
  confirmLabel?: string;
  onConfirm: () => void;
  onCancel: () => void;
  destructive?: boolean;
}

export function ConfirmDialog({
  open,
  title,
  message,
  confirmLabel,
  onConfirm,
  onCancel,
  destructive,
}: ConfirmProps) {
  const { t } = useTranslation();
  return (
    <Modal
      open={open}
      onClose={onCancel}
      title={title}
      className="w-[min(92vw,26rem)]"
      footer={
        <>
          <button
            type="button"
            onClick={onCancel}
            className="rounded-field border border-line-strong px-3 py-1.5 text-xs font-medium text-ink hover:bg-raised"
          >
            {t('app.cancel')}
          </button>
          <button
            type="button"
            onClick={onConfirm}
            className={cn(
              'rounded-field px-3 py-1.5 text-xs font-medium text-white',
              destructive ? 'bg-danger hover:opacity-90' : 'bg-accent hover:bg-accent-hover',
            )}
          >
            {confirmLabel ?? t('app.confirm')}
          </button>
        </>
      }
    >
      <div className="text-sm leading-relaxed text-ink-soft">{message}</div>
    </Modal>
  );
}