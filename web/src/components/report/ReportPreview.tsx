import { useQuery, useQueryClient } from '@tanstack/react-query';
import {
  Braces,
  Download,
  Eye,
  FileSpreadsheet,
  FileText,
  FileType2,
  Globe,
  Sheet,
  Table as TableIcon,
} from 'lucide-react';
import { useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';

import { InlineError, LoadingBlock } from '@/components/ErrorBoundary';
import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { EmptyState, Table, Td, Th } from '@/components/ui/feedback';
import { Tab, Tabs, TabsList } from '@/components/ui/tabs';
import { clampCell, parseCsvSections } from '@/lib/csv';
import { formatBytes } from '@/lib/format';
import { api, type RenderedExport } from '@/lib/api';
import { cn } from '@/lib/cn';
import { parseMarkdown, type MdAlign, type MdBlock, type MdSpan } from '@/lib/markdown';
import type { MetaOptions } from '@/lib/types';

/** How a format's artifact is shown. */
export type PreviewMode = 'html' | 'md' | 'txt' | 'csv' | 'json' | 'pdf' | 'xlsx';

const ICONS: Record<'report' | PreviewMode, React.ComponentType<{ className?: string }>> = {
  report: Eye,
  html: Globe,
  md: FileText,
  txt: FileType2,
  csv: TableIcon,
  json: Braces,
  pdf: Sheet,
  xlsx: FileSpreadsheet,
};

/** Rows rendered at once in the CSV preview before it truncates. */
const CSV_ROW_LIMIT = 50;

/** Lines kept in a scrolling text pane before the rest is cut. */
const TEXT_LINE_LIMIT = 2000;

export interface ReportPreviewProps {
  jobId: string;
  /** Required when the job produced several reports. */
  repo?: string;
  /** Formats the daemon offers, from /api/meta/options. */
  options: MetaOptions | undefined;
  /** Report language; a change re-renders the artifacts in the new language. */
  lang?: string;
  /** Rendered for the "Report" tab. */
  report: React.ReactNode;
  /** Extra controls placed beside the download button, e.g. a repo picker. */
  leading?: React.ReactNode;
}

export function ReportPreview({ jobId, repo, options, lang, report, leading }: ReportPreviewProps) {
  const { t } = useTranslation();
  const [mode, setMode] = useState<'report' | PreviewMode>('report');

  const modes = useMemo<(PreviewMode | 'report')[]>(() => {
    const offered = new Set((options?.formats ?? []).map((format) => format.id));
    const list: (PreviewMode | 'report')[] = ['report'];
    // Ordered the way the artifacts are meant to be read: markup first, then
    // the rendered pages, then the raw data.
    for (const id of ['html', 'md', 'txt', 'csv', 'json', 'pdf', 'xlsx'] as PreviewMode[])
      if (offered.has(id)) list.push(id);
    return list;
  }, [options]);

  // Fall back to the live report if the daemon's format list ever drops the
  // one that was selected.
  const active = modes.includes(mode) ? mode : 'report';

  const label = (entry: PreviewMode | 'report') =>
    entry === 'report' ? t('preview.report') : entry.toUpperCase();

  return (
    <div className="space-y-3">
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2 text-base">
            <Eye className="size-4" />
            {t('preview.title')}
          </CardTitle>
          <div className="flex items-center gap-2">
            {leading}
            {active === 'report' ? null : (
              <DownloadButton jobId={jobId} repo={repo} format={active} lang={lang} />
            )}
          </div>
        </CardHeader>
        <CardContent className="space-y-3">
          <Tabs value={active} onValueChange={(value) => setMode(value as PreviewMode)}>
            <TabsList>
              {modes.map((entry) => {
                const Icon = ICONS[entry];
                return (
                  <Tab key={entry} value={entry}>
                    <Icon />
                    {label(entry)}
                  </Tab>
                );
              })}
            </TabsList>
          </Tabs>

          <p className="text-xs text-muted">
            {active === 'report'
              ? t('preview.reportHint')
              : active === 'pdf'
                ? t('preview.pdfHint')
                : active === 'xlsx'
                  ? t('preview.xlsxHint')
                  : t('preview.artifactHint', { format: label(active) })}
          </p>
        </CardContent>
      </Card>

      {active === 'report' ? (
        report
      ) : (
        <ExportArtifact jobId={jobId} repo={repo} mode={active} lang={lang} />
      )}
    </div>
  );
}

/**
 * Fetches one rendered artifact and hands it to the viewer that format needs.
 * The query never goes stale: an export is derived from the analysis the job
 * already holds, so coming back to a mode should not re-render it.
 */
function ExportArtifact({
  jobId,
  repo,
  mode,
  lang,
}: {
  jobId: string;
  repo?: string;
  mode: PreviewMode;
  lang?: string;
}) {
  const { t } = useTranslation();
  const query = useQuery({
    // The language is part of the key: the daemon renders headings and labels
    // in it, so switching the report language must invalidate the preview rather
    // than serve a cached artifact in the previous one.
    queryKey: ['export', jobId, repo ?? '', mode, lang ?? ''],
    queryFn: () => api.fetchExport(jobId, mode, repo, lang),
    staleTime: Infinity,
    gcTime: 30 * 60_000,
  });

  // A reserved height keeps the page from jumping as the tab swaps content.
  if (query.isPending) return <LoadingBlock centered height={420} />;

  if (query.isError) {
    return (
      <Card>
        <CardContent className="space-y-3 pt-4">
          <InlineError
            message={query.error instanceof Error ? query.error.message : t('errors.generic')}
          />
          <Button variant="outline" size="sm" onClick={() => void query.refetch()}>
            {t('app.retry')}
          </Button>
        </CardContent>
      </Card>
    );
  }

  switch (mode) {
    case 'pdf':
      return <PdfArtifact artifact={query.data} />;
    case 'xlsx':
      return <BinaryArtifact artifact={query.data} />;
    case 'html':
      return <HtmlArtifact artifact={query.data} />;
    case 'md':
      return <MarkdownArtifact artifact={query.data} />;
    case 'csv':
      return <CsvArtifact artifact={query.data} />;
    case 'json':
      return <JsonArtifact artifact={query.data} />;
    default:
      return <TextArtifact artifact={query.data} />;
  }
}

function DownloadButton({
  jobId,
  repo,
  format,
  lang,
}: {
  jobId: string;
  repo?: string;
  format: PreviewMode;
  lang?: string;
}) {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const [busy, setBusy] = useState(false);

  return (
    <Button
      variant="primary"
      size="sm"
      disabled={busy}
      onClick={() => {
        setBusy(true);
        void api
          .downloadExportFor(jobId, format, repo, lang)
          .catch(() => undefined)
          .finally(() => {
            setBusy(false);
            void queryClient.invalidateQueries({ queryKey: ['quota'] });
          });
      }}
    >
      <Download />
      {busy ? t('app.saving') : t('preview.download')}
    </Button>
  );
}

/* ------------------------------------------------------------- artifacts */

function ArtifactHeader({ title, size }: { title: string; size: number }) {
  return (
    <CardHeader>
      <CardTitle className="truncate font-mono text-sm">{title}</CardTitle>
      <Badge tone="neutral" className="nums font-normal">
        {formatBytes(size)}
      </Badge>
    </CardHeader>
  );
}

function PdfArtifact({ artifact }: { artifact: RenderedExport }) {
  const url = useObjectUrl(artifact.blob);
  return (
    <Card>
      <ArtifactHeader title={artifact.filename} size={artifact.blob.size} />
      <CardContent className="px-0 pb-0">
        {url ? (
          <iframe
            title={artifact.filename}
            src={url}
            className="h-[70vh] w-full rounded-b-card border-t border-line bg-white"
          />
        ) : (
          <LoadingBlock centered height={420} />
        )}
      </CardContent>
    </Card>
  );
}

/**
 * Formats a browser cannot draw. Rather than pretend the artifact is empty,
 * say what the file is and hand it over.
 */
function BinaryArtifact({ artifact }: { artifact: RenderedExport }) {
  const { t } = useTranslation();
  return (
    <Card>
      <ArtifactHeader title={artifact.filename} size={artifact.blob.size} />
      <CardContent className="space-y-3 py-8 text-center">
        <FileSpreadsheet className="mx-auto size-8 text-muted" />
        <div className="space-y-1">
          <p className="text-sm text-ink">{t('preview.binaryTitle')}</p>
          <p className="text-xs text-muted">{artifact.contentType}</p>
          <p className="text-xs text-muted">{formatBytes(artifact.blob.size)}</p>
        </div>
      </CardContent>
    </Card>
  );
}

function HtmlArtifact({ artifact }: { artifact: RenderedExport }) {
  return (
    <Card>
      <ArtifactHeader title={artifact.filename} size={artifact.blob.size} />
      <CardContent className="px-0 pb-0">
        {/* The export carries its own stylesheet and a row-filter script, so it
            is shown exactly as produced. sandbox="allow-scripts" lets that
            script run while denying the frame access to the console's origin. */}
        <iframe
          title={artifact.filename}
          srcDoc={artifact.text ?? ''}
          sandbox="allow-scripts"
          className="h-[70vh] w-full rounded-b-card border-t border-line bg-white"
        />
      </CardContent>
    </Card>
  );
}

function MarkdownArtifact({ artifact }: { artifact: RenderedExport }) {
  const { t } = useTranslation();
  const source = artifact.text ?? '';
  const blocks = useMemo(() => parseMarkdown(source), [source]);

  return (
    <Card>
      <ArtifactHeader title={artifact.filename} size={artifact.blob.size} />
      <CardContent>
        {blocks.length === 0 ? (
          <EmptyState className="py-8" title={t('preview.empty')} />
        ) : (
          <div className="max-h-[70vh] space-y-3 overflow-auto pr-1 scrollbar-thin">
            {blocks.map((block, index) => (
              <MdBlockView key={index} block={block} />
            ))}
          </div>
        )}
      </CardContent>
    </Card>
  );
}

function TextArtifact({ artifact }: { artifact: RenderedExport }) {
  return (
    <Card>
      <ArtifactHeader title={artifact.filename} size={artifact.blob.size} />
      <CardContent>
        <TextPane text={artifact.text ?? ''} />
      </CardContent>
    </Card>
  );
}

function JsonArtifact({ artifact }: { artifact: RenderedExport }) {
  const pretty = useMemo(() => {
    try {
      return JSON.stringify(JSON.parse(artifact.text ?? ''), null, 2);
    } catch {
      return artifact.text ?? '';
    }
  }, [artifact.text]);

  return (
    <Card>
      <ArtifactHeader title={artifact.filename} size={artifact.blob.size} />
      <CardContent>
        <TextPane text={pretty} />
      </CardContent>
    </Card>
  );
}

function CsvArtifact({ artifact }: { artifact: RenderedExport }) {
  const { t } = useTranslation();
  const sections = useMemo(() => parseCsvSections(artifact.text ?? ''), [artifact.text]);

  if (sections.length === 0) {
    return (
      <Card>
        <ArtifactHeader title={artifact.filename} size={artifact.blob.size} />
        <CardContent>
          <EmptyState className="py-8" title={t('preview.empty')} />
        </CardContent>
      </Card>
    );
  }

  return (
    <div className="space-y-3">
      {sections.map((section, index) => {
        const shown = section.rows.slice(0, CSV_ROW_LIMIT);
        return (
          <Card key={`${section.title}-${index}`}>
            <CardHeader>
              <CardTitle className="text-sm">{section.title || artifact.filename}</CardTitle>
              <Badge tone="neutral" className="nums font-normal">
                {section.rows.length}
              </Badge>
            </CardHeader>
            <CardContent className="px-0 pb-0">
              <div className="max-h-[60vh] overflow-auto scrollbar-thin">
                <Table>
                  <thead className="sticky top-0 bg-surface">
                    <tr>
                      {section.header.map((cell, c) => (
                        <Th key={c}>{clampCell(cell, 60)}</Th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {shown.map((row, r) => (
                      <tr key={r} className="hover:bg-raised/60">
                        {row.map((cell, c) => (
                          <Td key={c} className="font-mono text-xs">
                            {clampCell(cell)}
                          </Td>
                        ))}
                      </tr>
                    ))}
                  </tbody>
                </Table>
              </div>
              {section.rows.length > shown.length ? (
                <p className="border-t border-line px-4 py-2 text-xs text-muted">
                  {t('preview.rowsTruncated', { shown: shown.length, total: section.rows.length })}
                </p>
              ) : null}
            </CardContent>
          </Card>
        );
      })}
    </div>
  );
}

function TextPane({ text }: { text: string }) {
  const { t } = useTranslation();
  if (!text) return <EmptyState className="py-8" title={t('preview.empty')} />;

  const lines = text.split('\n');
  const truncated = lines.length > TEXT_LINE_LIMIT;
  const shown = truncated ? lines.slice(0, TEXT_LINE_LIMIT).join('\n') : text;

  return (
    <>
      <pre className="max-h-[70vh] overflow-auto rounded-field bg-raised p-3 font-mono text-xs leading-relaxed whitespace-pre-wrap text-ink-soft scrollbar-thin">
        {shown}
      </pre>
      {truncated ? (
        <p className="mt-2 text-xs text-muted">
          {t('preview.linesTruncated', { shown: TEXT_LINE_LIMIT, total: lines.length })}
        </p>
      ) : null}
    </>
  );
}

/* ---------------------------------------------------------------- markdown */

const HEADING_SIZES = [
  'text-lg font-semibold',
  'text-base font-semibold',
  'text-sm font-semibold',
  'text-sm font-semibold',
  'text-xs font-semibold',
  'text-xs font-semibold',
];

function MdBlockView({ block }: { block: MdBlock }) {
  if (block.kind === 'hr') return <hr className="border-line" />;

  if (block.kind === 'code') {
    return (
      <pre className="overflow-x-auto rounded-field bg-raised p-3 font-mono text-xs leading-relaxed text-ink-soft scrollbar-thin">
        {block.text}
      </pre>
    );
  }

  if (block.kind === 'heading') {
    return (
      <p className={cn('text-ink', HEADING_SIZES[block.level - 1] ?? HEADING_SIZES[5])}>
        <MdSpans spans={block.spans} />
      </p>
    );
  }

  if (block.kind === 'table') {
    return (
      <div className="overflow-x-auto scrollbar-thin">
        <Table>
          <thead>
            <tr>
              {block.header.map((cell, index) => (
                <Th key={index} className={alignClass(block.align[index])}>
                  <MdSpans spans={cell} />
                </Th>
              ))}
            </tr>
          </thead>
          <tbody>
            {block.rows.map((row, r) => (
              <tr key={r} className="hover:bg-raised/60">
                {row.map((cell, c) => (
                  <Td key={c} className={alignClass(block.align[c])}>
                    <MdSpans spans={cell} />
                  </Td>
                ))}
              </tr>
            ))}
          </tbody>
        </Table>
      </div>
    );
  }

  return (
    <p className="text-sm leading-relaxed text-ink-soft">
      <MdSpans spans={block.spans} />
    </p>
  );
}

function MdSpans({ spans }: { spans: MdSpan[] }) {
  return (
    <>
      {spans.map((span, index) => (
        <MdSpanView key={index} span={span} />
      ))}
    </>
  );
}

function MdSpanView({ span }: { span: MdSpan }) {
  if (span.code) {
    return (
      <code className="rounded bg-raised px-1 py-0.5 font-mono text-[0.85em] text-ink">
        {span.text}
      </code>
    );
  }

  // The text node is escaped by React, so a repository title containing markup
  // is shown as text rather than becoming elements.
  let node: React.ReactNode = span.text;
  if (span.emphasis) node = <em>{node}</em>;
  if (span.strong) node = <strong className="font-semibold text-ink">{node}</strong>;
  if (span.href) {
    node = (
      <a href={span.href} target="_blank" rel="noreferrer" className="text-accent hover:underline">
        {node}
      </a>
    );
  }
  return <>{node}</>;
}

function alignClass(align: MdAlign | undefined): string {
  if (align === 'right') return 'text-right';
  if (align === 'center') return 'text-center';
  return 'text-left';
}

/* ----------------------------------------------------------------- helpers */

/**
 * Creates an object URL for a blob and revokes it when the blob changes or the
 * component goes away -- an object URL pins its blob in memory for the life of
 * the document otherwise.
 */
function useObjectUrl(blob: Blob): string | undefined {
  const [url, setUrl] = useState<string>();

  useEffect(() => {
    const next = URL.createObjectURL(blob);
    setUrl(next);
    return () => URL.revokeObjectURL(next);
  }, [blob]);

  return url;
}