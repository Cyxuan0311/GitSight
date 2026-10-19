import { create } from 'zustand';

export type Theme = 'light' | 'dark';

const THEME_KEY = 'gitsight.theme';
const SIDEBAR_KEY = 'gitsight.sidebar';

function initialTheme(): Theme {
  const stored = localStorage.getItem(THEME_KEY);
  if (stored === 'light' || stored === 'dark') return stored;
  return window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
}

function applyTheme(theme: Theme): void {
  document.documentElement.classList.toggle('dark', theme === 'dark');
}

interface UiState {
  theme: Theme;
  toggleTheme: () => void;
  setTheme: (theme: Theme) => void;
  /** md and below collapse the sidebar to an overlay drawer. */
  sidebarOpen: boolean;
  setSidebarOpen: (open: boolean) => void;
  /** Desktop rail collapse, independent of the mobile drawer. */
  sidebarCollapsed: boolean;
  toggleSidebarCollapsed: () => void;
}

export const useUiStore = create<UiState>((set, get) => ({
  theme: initialTheme(),
  setTheme: (theme) => {
    applyTheme(theme);
    localStorage.setItem(THEME_KEY, theme);
    set({ theme });
  },
  toggleTheme: () => {
    const next: Theme = get().theme === 'dark' ? 'light' : 'dark';
    get().setTheme(next);
  },

  sidebarOpen: false,
  setSidebarOpen: (open) => set({ sidebarOpen: open }),

  sidebarCollapsed: localStorage.getItem(SIDEBAR_KEY) === '1',
  toggleSidebarCollapsed: () => {
    const collapsed = !get().sidebarCollapsed;
    localStorage.setItem(SIDEBAR_KEY, collapsed ? '1' : '0');
    set({ sidebarCollapsed: collapsed });
  },
}));

// Apply once at startup so the first paint already has the right palette.
applyTheme(useUiStore.getState().theme);