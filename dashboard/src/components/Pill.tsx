import type { ReactNode } from 'react';

interface PillProps {
  children: ReactNode;
  tone?: 'neutral' | 'blaze';
}

export function Pill({ children, tone = 'neutral' }: PillProps) {
  const cls =
    tone === 'blaze'
      ? 'border-blaze/25 bg-blaze/10 text-blaze'
      : 'border-white/10 bg-white/[0.03] text-slate-400';
  return (
    <span
      className={`inline-flex items-center gap-1.5 rounded-md border px-2 py-0.5 font-mono text-[11px] ${cls}`}
    >
      {children}
    </span>
  );
}
