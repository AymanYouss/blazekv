export interface ShardStats {
  id: number;
  commands: number;
  keys: number;
  memory_bytes: number;
  connections: number;
  reads: number;
  writes: number;
  p99_us: number;
}

export interface Stats {
  uptime_s: number;
  shard_count: number;
  commands_total: number;
  keys: number;
  memory_bytes: number;
  connections: number;
  cross_shard_ops: number;
  keyspace_hits: number;
  keyspace_misses: number;
  latency_p50_us: number;
  latency_p99_us: number;
  latency_p999_us: number;
  mem_allocator: string;
  reactor: string;
  shards: ShardStats[];
}

export interface QueryResponse {
  result: string;
}

/**
 * One derived sample kept in the rolling client-side series. Throughput and
 * cross-shard rate are computed as deltas against the previous poll, so the
 * first sample after a (re)connect will report zero for those fields.
 */
export interface Sample {
  t: number;
  throughput: number;
  crossShardRate: number;
  p50_us: number;
  p99_us: number;
  p999_us: number;
  memory_bytes: number;
  connections: number;
  keys: number;
  hitRate: number;
}

export type ConnectionState = 'connecting' | 'live' | 'demo' | 'disconnected';

export interface ConsoleEntry {
  id: number;
  cmd: string;
  result: string;
  isError: boolean;
  ts: number;
}
