import {
  Activity,
  BarChart3,
  GitCompareArrows,
  LayoutDashboard,
  ListChecks,
  Server,
  Settings,
  Terminal,
  Users,
} from 'lucide-react';

import type { NavGroup } from './Sidebar';

/**
 * Navigation is grouped by intent rather than by resource type: what you came
 * to do (analysis), what you maintain (management), what you configure
 * (system).
 *
 * Kept apart from AppLayout so the route-level loading shell can render the
 * same rail while a page chunk is still in flight. The reader can then keep
 * moving between tabs instead of losing the navigation until the page lands.
 */
export const NAV_GROUPS: NavGroup[] = [
  {
    // Overview is a single top-level entry, so it needs no group heading.
    id: 'overview',
    items: [
      { to: '/', labelKey: 'nav.overview', icon: LayoutDashboard, exact: true },
    ],
  },
  {
    id: 'analyze',
    labelKey: 'nav.groupAnalyze',
    items: [
      { to: '/analyze', labelKey: 'nav.analyze', icon: Activity },
      { to: '/compare', labelKey: 'nav.compare', icon: GitCompareArrows },
      { to: '/batch', labelKey: 'nav.batch', icon: ListChecks },
    ],
  },
  {
    id: 'manage',
    labelKey: 'nav.groupManage',
    items: [
      { to: '/subscriptions', labelKey: 'nav.subscriptions', icon: Users },
      { to: '/usage', labelKey: 'nav.usage', icon: BarChart3 },
      { to: '/llm', labelKey: 'nav.llm', icon: Server },
    ],
  },
  {
    id: 'system',
    labelKey: 'nav.groupSystem',
    items: [
      { to: '/settings', labelKey: 'nav.settings', icon: Settings },
      { to: '/completion', labelKey: 'nav.completion', icon: Terminal },
    ],
  },
];
