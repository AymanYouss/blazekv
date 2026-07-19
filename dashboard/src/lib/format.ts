/**
 * Compact number formatting: 1234 -> "1.2K", 1_280_000 -> "1.28M".
 * Uses tabular-friendly fixed precision so figures don't jitter while polling.
 */
export function formatCompact(value: number, digits = 1): string {
  if (!Number.isFinite(value)) return '—';
  const abs = Math.abs(value);
  if (abs < 1000) {
    return abs < 10 && value % 1 !== 0 ? value.toFixed(digits) : String(Math.round(value));
  }
  const units = [
    { v: 1e12, s: 'T' },
    { v: 1e9, s: 'B' },
    { v: 1e6, s: 'M' },
    { v: 1e3, s: 'K' },
  ];
  for (const u of units) {
    if (abs >= u.v) {
      const scaled = value / u.v;
      const d = scaled >= 100 ? 0 : scaled >= 10 ? 1 : digits;
      return `${scaled.toFixed(d)}${u.s}`;
    }
  }
  return String(Math.round(value));
}

/** Full grouped integer, e.g. 10000 -> "10,000". */
export function formatInt(value: number): string {
  if (!Number.isFinite(value)) return '—';
  return Math.round(value).toLocaleString('en-US');
}

/** Byte sizes with binary-ish units: 52428800 -> "50.0 MB". */
export function formatBytes(bytes: number): { value: string; unit: string } {
  if (!Number.isFinite(bytes) || bytes <= 0) return { value: '0', unit: 'B' };
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let i = 0;
  let n = bytes;
  while (n >= 1024 && i < units.length - 1) {
    n /= 1024;
    i += 1;
  }
  const digits = n >= 100 || i === 0 ? 0 : n >= 10 ? 1 : 2;
  return { value: n.toFixed(digits), unit: units[i] };
}

/** Microsecond latency, auto-scaled to µs / ms. */
export function formatLatency(us: number): { value: string; unit: string } {
  if (!Number.isFinite(us) || us < 0) return { value: '—', unit: '' };
  if (us < 1000) {
    return { value: us >= 100 ? String(Math.round(us)) : us.toFixed(1), unit: 'µs' };
  }
  const ms = us / 1000;
  return { value: ms >= 100 ? String(Math.round(ms)) : ms.toFixed(2), unit: 'ms' };
}

/** Humanized uptime, e.g. 90061 -> "1d 1h 1m". */
export function formatUptime(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds < 0) return '—';
  const s = Math.floor(seconds);
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  if (d > 0) return `${d}d ${h}h ${m}m`;
  if (h > 0) return `${h}h ${m}m`;
  if (m > 0) return `${m}m ${sec}s`;
  return `${sec}s`;
}

export function formatPercent(fraction: number, digits = 1): string {
  if (!Number.isFinite(fraction)) return '—';
  return `${(fraction * 100).toFixed(digits)}%`;
}

export function clockLabel(ts: number): string {
  const d = new Date(ts);
  return d.toLocaleTimeString('en-US', { hour12: false });
}
