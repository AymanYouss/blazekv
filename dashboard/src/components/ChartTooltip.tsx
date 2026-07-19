import type { TooltipProps } from 'recharts';
import type { NameType, ValueType } from 'recharts/types/component/DefaultTooltipContent';
import { clockLabel } from '@/lib/format';

type Formatter = (value: number) => string;

interface Props extends TooltipProps<ValueType, NameType> {
  format: Formatter;
}

export function ChartTooltip({ active, payload, label, format }: Props) {
  if (!active || !payload || payload.length === 0) return null;
  return (
    <div className="rounded-md border border-white/10 bg-elevated/95 px-2.5 py-1.5 shadow-card backdrop-blur">
      <div className="mb-1 font-mono text-[10px] uppercase tracking-wider text-slate-500">
        {typeof label === 'number' ? clockLabel(label) : String(label ?? '')}
      </div>
      {payload.map((p) => (
        <div key={String(p.dataKey)} className="flex items-center gap-2 text-[11px]">
          <span
            className="h-1.5 w-1.5 rounded-full"
            style={{ backgroundColor: (p.color as string) ?? '#F5A623' }}
          />
          <span className="text-slate-400">{p.name}</span>
          <span className="tnum ml-auto font-mono text-slate-100">
            {format(Number(p.value))}
          </span>
        </div>
      ))}
    </div>
  );
}
