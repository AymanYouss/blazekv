import { Card } from './Card';
import { Pill } from './Pill';
import { formatBytes, formatCompact, formatLatency, formatPercent } from '@/lib/format';
import type { Stats } from '@/types';

interface Props {
  stats: Stats | null;
}

/**
 * Per-shard load balance. In a thread-per-core shared-nothing design each shard
 * owns a disjoint slice of the keyspace, so an even bar distribution here is the
 * visual signal that hashing and routing are balanced.
 */
export function ShardPanel({ stats }: Props) {
  const shards = stats?.shards ?? [];
  const totalCmds = shards.reduce((a, s) => a + s.commands, 0) || 1;
  const maxShare = Math.max(...shards.map((s) => s.commands / totalCmds), 0.0001);

  return (
    <Card
      title="Shard load balance"
      subtitle="thread-per-core · shared-nothing"
      right={
        <div className="flex items-center gap-1.5">
          {stats && <Pill tone="blaze">{stats.reactor}</Pill>}
          {stats && <Pill>{stats.mem_allocator}</Pill>}
        </div>
      }
    >
      <div className="px-4 pb-4 pt-3">
        {shards.length === 0 ? (
          <div className="space-y-2.5">
            {Array.from({ length: 8 }).map((_, i) => (
              <div key={i} className="h-7 animate-pulse rounded bg-white/[0.03]" />
            ))}
          </div>
        ) : (
          <div className="space-y-1.5">
            {shards.map((s) => {
              const share = s.commands / totalCmds;
              const width = Math.max(2, (share / maxShare) * 100);
              const readRatio = s.reads + s.writes > 0 ? s.reads / (s.reads + s.writes) : 0;
              const lat = formatLatency(s.p99_us);
              const mem = formatBytes(s.memory_bytes);
              return (
                <div
                  key={s.id}
                  className="grid grid-cols-[40px_1fr_auto] items-center gap-3 rounded-md px-1.5 py-1 transition-colors hover:bg-white/[0.02]"
                >
                  <span className="font-mono text-xs text-slate-500">
                    #{String(s.id).padStart(2, '0')}
                  </span>

                  <div className="min-w-0">
                    <div className="h-2 w-full overflow-hidden rounded-full bg-white/[0.04]">
                      <div
                        className="h-full rounded-full bg-gradient-to-r from-blaze/70 to-blaze"
                        style={{ width: `${width}%` }}
                      />
                    </div>
                    <div className="mt-1 flex items-center gap-2 font-mono text-[10px] text-slate-500">
                      <span className="text-slate-400">{formatPercent(share)}</span>
                      <span>·</span>
                      <span>{formatCompact(s.keys)} keys</span>
                      <span>·</span>
                      <span>{mem.value}{mem.unit}</span>
                      <span className="hidden sm:inline">·</span>
                      <span className="hidden sm:inline">
                        r/w {(readRatio * 100).toFixed(0)}/{(100 - readRatio * 100).toFixed(0)}
                      </span>
                    </div>
                  </div>

                  <div className="text-right">
                    <span className="tnum font-mono text-xs text-slate-300">
                      {lat.value}
                      <span className="text-slate-600">{lat.unit}</span>
                    </span>
                    <div className="font-mono text-[10px] text-slate-600">
                      {s.connections} conn
                    </div>
                  </div>
                </div>
              );
            })}
          </div>
        )}
      </div>
    </Card>
  );
}
