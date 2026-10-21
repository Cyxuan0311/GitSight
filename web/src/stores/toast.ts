import { create } from 'zustand';

export type ToastTone = 'success' | 'error';

export interface Toast {
  id: number;
  tone: ToastTone;
  message: string;
}

/** How long a message stays on screen before it fades out. */
const DISMISS_AFTER = 4000;

/** The oldest messages are dropped first, so a burst cannot fill the screen. */
const MAX_TOASTS = 4;

interface ToastState {
  toasts: Toast[];
  push: (tone: ToastTone, message: string) => void;
  dismiss: (id: number) => void;
}

let nextId = 1;

export const useToastStore = create<ToastState>((set, get) => ({
  toasts: [],

  push: (tone, message) => {
    const toast: Toast = { id: nextId++, tone, message };
    set((state) => ({ toasts: [...state.toasts, toast].slice(-MAX_TOASTS) }));
    // The timer is deliberately not stored: a toast that is dismissed by hand
    // simply never fires its own removal.
    setTimeout(() => get().dismiss(toast.id), DISMISS_AFTER);
  },

  dismiss: (id) => set((state) => ({ toasts: state.toasts.filter((entry) => entry.id !== id) })),
}));

/**
 * The two ways a write can end.
 *
 * A console that mutates silently is a console that cannot be trusted: the
 * reader has no way to tell a saved change from a failed one.
 */
export const toast = {
  success: (message: string) => useToastStore.getState().push('success', message),
  error: (message: string) => useToastStore.getState().push('error', message),
};
