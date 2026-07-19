import { formatInt, formatPercent, formatUptime } from '@/lib/format';
import type { Sample, Stats } from '@/types';

interface Props {
  stats: Stats | null;
  latest: Sample | null;
}

function Item({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex flex-col">
      <span className="text-[10px] uppercase tracking-wider text-slate-600">{label}</span>
      <span className="tnum font-mono text-[13px] text-slate-200">{value}</span>
    </div>
  );
}

export function HeaderStats({ stats, latest }: Props) {
  return (
    <div className="hidden items-center gap-6 md:flex">
      <Item label="Uptime" value={stats ? formatUptime(stats.uptime_s) : '—'} />
      <div className="h-7 w-px bg-white/5" />
      <Item label="Shards" value={stats ? String(stats.shard_count) : '—'} />
      <div className="h-7 w-px bg-white/5" />
      <Item label="Hit rate" value={latest ? formatPercent(latest.hitRate) : '—'} />
      <div className="h-7 w-px bg-white/5" />
      <Item
        label="Commands"
        value={stats ? formatInt(stats.commands_total) : '—'}
      />
    </div>
  );
}
