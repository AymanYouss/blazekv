import type { ReactNode } from 'react';

interface CardProps {
  title?: string;
  subtitle?: string;
  right?: ReactNode;
  children: ReactNode;
  className?: string;
}

/** Base surface used for every panel: rounded, 1px hairline border, flat shadow. */
export function Card({ title, subtitle, right, children, className }: CardProps) {
  return (
    <section
      className={`rounded-lg border border-white/5 bg-surface shadow-card ${className ?? ''}`}
    >
      {(title || right) && (
        <header className="flex items-center justify-between gap-4 px-4 pt-3.5">
          <div>
            {title && (
              <h2 className="text-[13px] font-semibold tracking-tight text-slate-200">{title}</h2>
            )}
            {subtitle && <p className="mt-0.5 text-xs text-slate-500">{subtitle}</p>}
          </div>
          {right}
        </header>
      )}
      {children}
    </section>
  );
}
