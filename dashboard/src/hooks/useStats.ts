import { useCallback, useEffect, useRef, useState } from 'react';
import { fetchStats } from '@/lib/api';
import { nextMockStats } from '@/lib/mock';
import type { ConnectionState, Sample, Stats } from '@/types';

const POLL_MS = 1000;
const MAX_SAMPLES = 60;
// After this many consecutive failed polls we fall back to synthesized demo
// data so the dashboard never renders an empty/broken shell.
const DEMO_AFTER_FAILURES = 2;

interface Prev {
  commandsTotal: number;
  crossShardOps: number;
  t: number;
}

function deriveSample(stats: Stats, prev: Prev | null, now: number): Sample {
  let throughput = 0;
  let crossShardRate = 0;
  if (prev) {
    const dt = Math.max(0.001, (now - prev.t) / 1000);
    throughput = Math.max(0, (stats.commands_total - prev.commandsTotal) / dt);
    crossShardRate = Math.max(0, (stats.cross_shard_ops - prev.crossShardOps) / dt);
  }
  const totalLookups = stats.keyspace_hits + stats.keyspace_misses;
  const hitRate = totalLookups > 0 ? stats.keyspace_hits / totalLookups : 0;
  return {
    t: now,
    throughput,
    crossShardRate,
    p50_us: stats.latency_p50_us,
    p99_us: stats.latency_p99_us,
    p999_us: stats.latency_p999_us,
    memory_bytes: stats.memory_bytes,
    connections: stats.connections,
    keys: stats.keys,
    hitRate,
  };
}

export interface UseStatsResult {
  stats: Stats | null;
  series: Sample[];
  connection: ConnectionState;
  latest: Sample | null;
  previous: Sample | null;
}

export function useStats(): UseStatsResult {
  const [stats, setStats] = useState<Stats | null>(null);
  const [series, setSeries] = useState<Sample[]>([]);
  const [connection, setConnection] = useState<ConnectionState>('connecting');

  const prevRef = useRef<Prev | null>(null);
  const failuresRef = useRef(0);
  const connectionModeRef = useRef<ConnectionState>('connecting');
  connectionModeRef.current = connection;

  const push = useCallback((next: Stats, mode: 'live' | 'demo') => {
    const now = Date.now();
    const sample = deriveSample(next, prevRef.current, now);
    prevRef.current = {
      commandsTotal: next.commands_total,
      crossShardOps: next.cross_shard_ops,
      t: now,
    };
    setStats(next);
    setConnection(mode);
    setSeries((prev) => {
      const merged = [...prev, sample];
      return merged.length > MAX_SAMPLES ? merged.slice(merged.length - MAX_SAMPLES) : merged;
    });
  }, []);

  useEffect(() => {
    let cancelled = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

    const tick = async () => {
      try {
        const next = await fetchStats();
        if (cancelled) return;
        failuresRef.current = 0;
        push(next, 'live');
      } catch {
        if (cancelled) return;
        failuresRef.current += 1;
        if (failuresRef.current >= DEMO_AFTER_FAILURES) {
          // Switch to synthesized data; resets the delta baseline once.
          if (connectionModeRef.current !== 'demo') prevRef.current = null;
          push(nextMockStats(), 'demo');
        } else {
          setConnection('disconnected');
        }
      } finally {
        if (!cancelled) timer = setTimeout(tick, POLL_MS);
      }
    };

    // Track the last committed mode without adding it to the effect deps.
    void tick();

    return () => {
      cancelled = true;
      if (timer) clearTimeout(timer);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [push]);

  const latest = series.length ? series[series.length - 1] : null;
  const previous = series.length > 1 ? series[series.length - 2] : null;

  return { stats, series, connection, latest, previous };
}
