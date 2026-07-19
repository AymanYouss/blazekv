import type { Stats } from '@/types';

/**
 * Deterministic-ish demo data generator used when the BlazeKV server is
 * unreachable. Produces a believable thread-per-core profile: 8 shards,
 * throughput oscillating around ~1.2M ops/sec, p99 near 90µs, memory that
 * slowly climbs. This keeps the dashboard alive for screenshots.
 */

const SHARD_COUNT = 8;
const startedAt = Date.now();

// Persist accumulating counters across calls so rates come out sane.
const state = {
  commandsTotal: 4_820_000,
  crossShardOps: 61_200,
  keyspaceHits: 3_910_000,
  keyspaceMisses: 190_000,
  memoryBytes: 512 * 1024 * 1024,
  keys: 1_240_000,
  // Per-shard skew so the load-balance panel looks organic but even-ish.
  shardWeights: [1.03, 0.98, 1.01, 0.96, 1.05, 0.99, 1.02, 0.96],
  shardCommands: [] as number[],
  shardKeys: [] as number[],
};

for (let i = 0; i < SHARD_COUNT; i += 1) {
  state.shardCommands.push(Math.round((state.commandsTotal / SHARD_COUNT) * state.shardWeights[i]));
  state.shardKeys.push(Math.round((state.keys / SHARD_COUNT) * state.shardWeights[i]));
}

function noise(base: number, amp: number, phase: number): number {
  const t = (Date.now() - startedAt) / 1000;
  return base + Math.sin(t * 0.7 + phase) * amp * 0.6 + (Math.random() - 0.5) * amp * 0.8;
}

export function nextMockStats(): Stats {
  const uptimeS = Math.floor((Date.now() - startedAt) / 1000) + 91_734;

  // ~1.2M ops/sec spread across shards, sampled at the 1s cadence.
  const opsThisTick = Math.max(200_000, Math.round(noise(1_200_000, 240_000, 0)));
  const crossThisTick = Math.max(2_000, Math.round(noise(11_500, 3_500, 1.4)));
  const hitsThisTick = Math.round(opsThisTick * 0.94);
  const missThisTick = opsThisTick - hitsThisTick;

  state.commandsTotal += opsThisTick;
  state.crossShardOps += crossThisTick;
  state.keyspaceHits += hitsThisTick;
  state.keyspaceMisses += missThisTick;
  state.keys += Math.round(noise(120, 260, 2.2));
  state.memoryBytes += Math.round(noise(180_000, 900_000, 3.1));

  const shards = [];
  let assignedCmds = 0;
  let assignedKeys = 0;
  for (let i = 0; i < SHARD_COUNT; i += 1) {
    const w = state.shardWeights[i];
    const shardOps = Math.round((opsThisTick / SHARD_COUNT) * w);
    state.shardCommands[i] += shardOps;
    state.shardKeys[i] = Math.max(0, Math.round((state.keys / SHARD_COUNT) * w));
    assignedCmds += state.shardCommands[i];
    assignedKeys += state.shardKeys[i];
    const reads = Math.round(shardOps * (0.72 + (Math.random() - 0.5) * 0.05));
    shards.push({
      id: i,
      commands: state.shardCommands[i],
      keys: state.shardKeys[i],
      memory_bytes: Math.round((state.memoryBytes / SHARD_COUNT) * w),
      connections: Math.max(1, Math.round(noise(5.5, 2.5, i))),
      reads,
      writes: Math.max(0, shardOps - reads),
      p99_us: Math.max(30, Math.round(noise(84, 24, i * 0.5))),
    });
  }

  // Keep top-level totals consistent with the per-shard breakdown.
  state.commandsTotal = assignedCmds;
  state.keys = assignedKeys;

  return {
    uptime_s: uptimeS,
    shard_count: SHARD_COUNT,
    commands_total: state.commandsTotal,
    keys: state.keys,
    memory_bytes: Math.round(state.memoryBytes),
    connections: Math.max(SHARD_COUNT, Math.round(noise(42, 8, 0.9))),
    cross_shard_ops: state.crossShardOps,
    keyspace_hits: state.keyspaceHits,
    keyspace_misses: state.keyspaceMisses,
    latency_p50_us: Math.max(6, Math.round(noise(15, 4, 0.3))),
    latency_p99_us: Math.max(40, Math.round(noise(90, 20, 1.1))),
    latency_p999_us: Math.max(120, Math.round(noise(250, 60, 2.0))),
    mem_allocator: 'mimalloc',
    reactor: 'io_uring',
    shards,
  };
}

const MOCK_KV = new Map<string, string>([
  ['session:8f2a', '{"user":42,"scope":"rw"}'],
  ['user:42:name', 'ada'],
  ['flag:new_console', 'true'],
]);

/** Minimal offline responder for the query console in demo mode. */
export function mockQuery(cmd: string): string {
  const trimmed = cmd.trim();
  if (!trimmed) return '(error) empty command';
  const parts = trimmed.split(/\s+/);
  const verb = parts[0].toUpperCase();

  switch (verb) {
    case 'PING':
      return parts[1] ? `"${parts.slice(1).join(' ')}"` : 'PONG';
    case 'GET': {
      if (parts.length < 2) return "(error) ERR wrong number of arguments for 'get'";
      const v = MOCK_KV.get(parts[1]);
      return v === undefined ? '(nil)' : `"${v}"`;
    }
    case 'SET': {
      if (parts.length < 3) return "(error) ERR wrong number of arguments for 'set'";
      MOCK_KV.set(parts[1], parts.slice(2).join(' '));
      return 'OK';
    }
    case 'DEL': {
      let n = 0;
      for (const k of parts.slice(1)) if (MOCK_KV.delete(k)) n += 1;
      return `(integer) ${n}`;
    }
    case 'MGET': {
      const lines = parts.slice(1).map((k, i) => {
        const v = MOCK_KV.get(k);
        return `${i + 1}) ${v === undefined ? '(nil)' : `"${v}"`}`;
      });
      return lines.length ? lines.join('\n') : '(empty array)';
    }
    case 'KEYS':
      return [...MOCK_KV.keys()].map((k, i) => `${i + 1}) "${k}"`).join('\n') || '(empty array)';
    case 'DBSIZE':
      return `(integer) ${MOCK_KV.size}`;
    case 'INFO':
      return [
        '# Server',
        'blazekv_version:1.0.0',
        'reactor:io_uring',
        'mem_allocator:mimalloc',
        'architecture:thread-per-core shared-nothing',
        'shards:8',
      ].join('\n');
    default:
      return `(error) ERR unknown command '${parts[0]}'`;
  }
}
