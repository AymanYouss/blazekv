import { HeaderStats } from '@/components/HeaderStats';
import { KpiRow } from '@/components/KpiRow';
import { QueryConsole } from '@/components/QueryConsole';
import { ShardPanel } from '@/components/ShardPanel';
import { StatusDot } from '@/components/StatusDot';
import { TimeSeriesCharts } from '@/components/TimeSeriesCharts';
import { Wordmark } from '@/components/Wordmark';
import { useStats } from '@/hooks/useStats';

export default function App() {
  const { stats, series, connection, latest, previous } = useStats();
  const loading = series.length === 0;

  return (
    <div className="min-h-screen bg-canvas">
      <header className="sticky top-0 z-20 border-b border-white/5 bg-canvas/80 backdrop-blur-md">
        <div className="mx-auto flex h-14 max-w-[1440px] items-center justify-between gap-6 px-5">
          <div className="flex items-center gap-8">
            <Wordmark />
            <HeaderStats stats={stats} latest={latest} />
          </div>
          <StatusDot state={connection} />
        </div>
      </header>

      <main className="mx-auto max-w-[1440px] space-y-3 px-5 py-5">
        <KpiRow series={series} latest={latest} previous={previous} loading={loading} />

        <TimeSeriesCharts series={series} />

        <div className="grid grid-cols-1 gap-3 lg:grid-cols-2">
          <ShardPanel stats={stats} />
          <QueryConsole demo={connection === 'demo'} />
        </div>

        <footer className="flex items-center justify-between pt-2 pb-4 text-[11px] text-slate-600">
          <span>
            BlazeKV · Redis-compatible in-memory database · polling{' '}
            <span className="font-mono text-slate-500">/api/stats</span> @ 1s
          </span>
          <span className="font-mono">
            {stats ? `${stats.reactor} · ${stats.mem_allocator}` : '—'}
          </span>
        </footer>
      </main>
    </div>
  );
}
