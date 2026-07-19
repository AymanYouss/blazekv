import { Zap } from 'lucide-react';

export function Wordmark() {
  return (
    <div className="flex items-center gap-2.5">
      <span className="flex h-7 w-7 items-center justify-center rounded-md border border-blaze/25 bg-blaze/10">
        <Zap className="h-4 w-4 text-blaze" strokeWidth={2.5} fill="currentColor" />
      </span>
      <div className="leading-none">
        <div className="text-[15px] font-semibold tracking-tight text-white">
          Blaze<span className="text-blaze">KV</span>
        </div>
        <div className="mt-0.5 text-[10px] uppercase tracking-[0.18em] text-slate-600">
          Console
        </div>
      </div>
    </div>
  );
}
