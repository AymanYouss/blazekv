import type { QueryResponse, Stats } from '@/types';

const TIMEOUT_MS = 900;

async function withTimeout<T>(p: (signal: AbortSignal) => Promise<T>): Promise<T> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
  try {
    return await p(ctrl.signal);
  } finally {
    clearTimeout(timer);
  }
}

export async function fetchStats(): Promise<Stats> {
  return withTimeout(async (signal) => {
    const res = await fetch('/api/stats', {
      signal,
      headers: { accept: 'application/json' },
    });
    if (!res.ok) throw new Error(`stats ${res.status}`);
    return (await res.json()) as Stats;
  });
}

export async function runQuery(cmd: string): Promise<QueryResponse> {
  const res = await fetch('/api/query', {
    method: 'POST',
    headers: { 'content-type': 'application/json', accept: 'application/json' },
    body: JSON.stringify({ cmd }),
  });
  if (!res.ok) throw new Error(`query ${res.status}`);
  return (await res.json()) as QueryResponse;
}
