import {
  Activity,
  CalendarDays,
  CircleCheck,
  CircleDot,
  Clock,
  Code2,
  Eye,
  GitBranch,
  GitCommit,
  GitFork,
  GitMerge,
  GitPullRequest,
  Package,
  Scale,
  Sparkles,
  Star,
  TriangleAlert,
  Users,
} from 'lucide-react';
import type { ComponentType } from 'react';

import { cn } from '@/lib/cn';

/**
 * The marks that identify a report block.
 *
 * Every block of the report names a kind of activity, and the name alone makes
 * the reader work out what it holds. The icon says it at a glance, and it
 * gives the eye something to land on while scrolling a long report.
 */
const ICONS = {
  summary: Sparkles,
  activity: Activity,
  weekday: CalendarDays,
  languages: Code2,
  warnings: TriangleAlert,
  pullRequests: GitPullRequest,
  commits: GitCommit,
  issues: CircleDot,
  releases: Package,
  contributors: Users,
  merge: GitMerge,
  close: CircleCheck,
  busiest: CalendarDays,
  stars: Star,
  forks: GitFork,
  watchers: Eye,
  license: Scale,
  branch: GitBranch,
  pushed: Clock,
} satisfies Record<string, ComponentType<{ className?: string }>>;

export type ReportIcon = keyof typeof ICONS;

/**
 * The icon for a report block. It is decorative: the title beside it already
 * names the block, so a screen reader is told about it once.
 */
export function ReportSectionIcon({
  name,
  className,
}: {
  name: ReportIcon;
  className?: string;
}) {
  const Icon = ICONS[name];
  return <Icon className={cn('size-4 shrink-0 text-muted', className)} aria-hidden="true" />;
}
