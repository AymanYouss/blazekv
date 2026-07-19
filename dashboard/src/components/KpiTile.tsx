import { ArrowDownRight, ArrowUpRight } from 'lucide-react';
import { Sparkline } from './Sparkline';

export interface KpiTileProps {
  label: string;
  value: string;
  unit?: string;
  spark: number[];
  /** Signed relative change vs the previous sample, e.g. 0.031 = +3.1%. */
  delta: number | null;
  /** Whether a positive delta is "good" (green) or "bad" (red). */
  goodWhenUp?: boolean;
  loading?: boolean;
}

export function KpiTile({
  label,
  value,
  unit,
  spark,
  delta,
  goodWhenUp = true,
  loading = false,
}: KpiTileProps) {
  const hasDelta = delta !== null && Number.isFinite(delta) && Math.abs(delta) >= 0.0005;
  const up = (delta ?? 0) > 0;
  const isGood = hasDelta ? up === goodWhenUp : true;
  const deltaColor = !hasDelta ? 'text-slate-600' : isGood ? 'text-good' : 'text-bad';

  return (
    <div className="group relative overflow-hidden rounded-lg border border-white/5 bg-surface p-4 shadow-card transition-colors hover:border-white/10">
      <div className="flex items-start justify-between">
        <span className="text-[11px] font-medium uppercase tracking-wider text-slate-500">
          {label}
        </span>
        {hasDelta && (
          <span className={`flex items-center gap-0.5 font-mono text-[11px] ${deltaColor}`}>
            {up ? (
              <ArrowUpRight className="h-3 w-3" strokeWidth={2.5} />
            ) : (
              <ArrowDownRight className="h-3 w-3" strokeWidth={2.5} />
            )}
            {(Math.abs(delta ?? 0) * 100).toFixed(1)}%
          </span>
        )}
      </div>

      <div className="mt-2 flex items-baseline gap-1">
        {loading ? (
          <div className="h-8 w-24 animate-pulse rounded bg-white/5" />
        ) : (
          <>
            <span className="tnum font-mono text-[28px] font-semibold leading-none text-white">
              {value}
            </span>
            {unit && <span className="text-sm font-medium text-slate-500">{unit}</span>}
          </>
        )}
      </div>

      <div className="mt-3 h-[30px]">
        {!loading && <Sparkline data={spark} />}
      </div>
    </div>
  );
}
