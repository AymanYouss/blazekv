import { KpiTile } from './KpiTile';
import { formatBytes, formatCompact, formatInt, formatLatency } from '@/lib/format';
import type { Sample } from '@/types';

interface KpiRowProps {
  series: Sample[];
  latest: Sample | null;
  previous: Sample | null;
  loading: boolean;
}

function delta(cur: number, prev: number | undefined): number | null {
  if (prev === undefined || prev === 0) return null;
  return (cur - prev) / prev;
}

function tail(series: Sample[], pick: (s: Sample) => number): number[] {
  return series.map(pick);
}

export function KpiRow({ series, latest, previous, loading }: KpiRowProps) {
  const mem = latest ? formatBytes(latest.memory_bytes) : { value: '—', unit: '' };
  const p99 = latest ? formatLatency(latest.p99_us) : { value: '—', unit: '' };

  return (
    <div className="grid grid-cols-2 gap-3 md:grid-cols-3 xl:grid-cols-6">
      <KpiTile
        label="Throughput"
        value={latest ? formatCompact(latest.throughput) : '—'}
        unit="ops/s"
        spark={tail(series, (s) => s.throughput)}
        delta={latest ? delta(latest.throughput, previous?.throughput) : null}
        goodWhenUp
        loading={loading}
      />
      <KpiTile
        label="p99 Latency"
        value={p99.value}
        unit={p99.unit}
        spark={tail(series, (s) => s.p99_us)}
        delta={latest ? delta(latest.p99_us, previous?.p99_us) : null}
        goodWhenUp={false}
        loading={loading}
      />
      <KpiTile
        label="Memory"
        value={mem.value}
        unit={mem.unit}
        spark={tail(series, (s) => s.memory_bytes)}
        delta={latest ? delta(latest.memory_bytes, previous?.memory_bytes) : null}
        goodWhenUp={false}
        loading={loading}
      />
      <KpiTile
        label="Clients"
        value={latest ? formatInt(latest.connections) : '—'}
        spark={tail(series, (s) => s.connections)}
        delta={latest ? delta(latest.connections, previous?.connections) : null}
        goodWhenUp
        loading={loading}
      />
      <KpiTile
        label="Keys"
        value={latest ? formatCompact(latest.keys) : '—'}
        spark={tail(series, (s) => s.keys)}
        delta={latest ? delta(latest.keys, previous?.keys) : null}
        goodWhenUp
        loading={loading}
      />
      <KpiTile
        label="Cross-shard"
        value={latest ? formatCompact(latest.crossShardRate) : '—'}
        unit="ops/s"
        spark={tail(series, (s) => s.crossShardRate)}
        delta={latest ? delta(latest.crossShardRate, previous?.crossShardRate) : null}
        goodWhenUp={false}
        loading={loading}
      />
    </div>
  );
}
