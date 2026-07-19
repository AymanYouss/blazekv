import type { ConnectionState } from '@/types';

const MAP: Record<ConnectionState, { color: string; label: string; pulse: boolean }> = {
  connecting: { color: '#94a3b8', label: 'Connecting', pulse: true },
  live: { color: '#4ADE80', label: 'Connected', pulse: false },
  demo: { color: '#F5A623', label: 'Demo data', pulse: false },
  disconnected: { color: '#F87171', label: 'Disconnected', pulse: true },
};

export function StatusDot({ state }: { state: ConnectionState }) {
  const cfg = MAP[state];
  return (
    <div className="flex items-center gap-2 rounded-md border border-white/5 bg-white/[0.02] px-2.5 py-1">
      <span className="relative flex h-2 w-2">
        {cfg.pulse && (
          <span
            className="absolute inline-flex h-full w-full animate-pulsedot rounded-full opacity-70"
            style={{ backgroundColor: cfg.color }}
          />
        )}
        <span
          className="relative inline-flex h-2 w-2 rounded-full"
          style={{ backgroundColor: cfg.color }}
        />
      </span>
      <span className="text-[11px] font-medium text-slate-400">{cfg.label}</span>
    </div>
  );
}
