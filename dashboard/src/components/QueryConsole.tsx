import { useEffect, useRef, useState } from 'react';
import { CornerDownLeft, Terminal } from 'lucide-react';
import { Card } from './Card';
import { runQuery } from '@/lib/api';
import { mockQuery } from '@/lib/mock';
import type { ConsoleEntry } from '@/types';

interface Props {
  /** When true (server unreachable), queries are answered by the local mock. */
  demo: boolean;
}

const EXAMPLES = ['INFO', 'GET user:42:name', 'SET flag:new_console true', 'MGET user:42:name flag:new_console'];

function isErrorResult(result: string): boolean {
  return result.trimStart().startsWith('(error)');
}

export function QueryConsole({ demo }: Props) {
  const [input, setInput] = useState('');
  const [entries, setEntries] = useState<ConsoleEntry[]>([]);
  const [busy, setBusy] = useState(false);
  const idRef = useRef(0);
  const scrollRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    const el = scrollRef.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [entries]);

  const submit = async (raw: string) => {
    const cmd = raw.trim();
    if (!cmd || busy) return;
    setBusy(true);
    setInput('');

    let result: string;
    try {
      if (demo) {
        result = mockQuery(cmd);
      } else {
        const res = await runQuery(cmd);
        result = res.result;
      }
    } catch {
      result = '(error) request failed — server unreachable';
    }

    idRef.current += 1;
    setEntries((prev) => {
      const next = [
        ...prev,
        { id: idRef.current, cmd, result, isError: isErrorResult(result), ts: Date.now() },
      ];
      return next.length > 100 ? next.slice(next.length - 100) : next;
    });
    setBusy(false);
    inputRef.current?.focus();
  };

  return (
    <Card
      title="Query console"
      subtitle={demo ? 'offline · responses simulated locally' : 'live · POST /api/query'}
      right={
        <div className="flex items-center gap-2">
          <Terminal className="h-3.5 w-3.5 text-slate-600" />
          <span className="font-mono text-[11px] text-slate-600">blaze&gt;</span>
        </div>
      }
    >
      <div className="px-4 pb-4 pt-2">
        <div className="mb-2 flex flex-wrap gap-1.5">
          {EXAMPLES.map((ex) => (
            <button
              key={ex}
              type="button"
              onClick={() => {
                setInput(ex);
                inputRef.current?.focus();
              }}
              className="rounded-md border border-white/5 bg-white/[0.02] px-2 py-1 font-mono text-[11px] text-slate-400 transition-colors hover:border-blaze/30 hover:text-blaze"
            >
              {ex}
            </button>
          ))}
        </div>

        <div
          ref={scrollRef}
          className="scroll-slim mb-2.5 h-52 overflow-y-auto rounded-md border border-white/5 bg-canvas/60 p-3 font-mono text-[12.5px] leading-relaxed"
        >
          {entries.length === 0 ? (
            <p className="text-slate-600">
              Type a command and press Enter. Try one of the examples above.
            </p>
          ) : (
            <div className="space-y-2">
              {entries.map((e) => (
                <div key={e.id} className="animate-fadein">
                  <div className="flex items-center gap-2">
                    <span className="select-none text-blaze/70">blaze&gt;</span>
                    <span className="text-blaze">{e.cmd}</span>
                  </div>
                  <pre
                    className={`mt-0.5 whitespace-pre-wrap break-words pl-[3.4rem] ${
                      e.isError ? 'text-bad/90' : 'text-slate-300'
                    }`}
                  >
                    {e.result}
                  </pre>
                </div>
              ))}
            </div>
          )}
        </div>

        <form
          onSubmit={(ev) => {
            ev.preventDefault();
            void submit(input);
          }}
          className="flex items-center gap-2"
        >
          <div className="flex flex-1 items-center gap-2 rounded-md border border-white/10 bg-canvas/60 px-3 py-2 focus-within:border-blaze/40">
            <span className="select-none font-mono text-sm text-blaze/70">blaze&gt;</span>
            <input
              ref={inputRef}
              value={input}
              onChange={(e) => setInput(e.target.value)}
              spellCheck={false}
              autoComplete="off"
              autoCapitalize="off"
              placeholder="GET mykey"
              className="flex-1 bg-transparent font-mono text-sm text-slate-100 placeholder:text-slate-600 focus:outline-none"
            />
          </div>
          <button
            type="submit"
            disabled={busy || !input.trim()}
            className="inline-flex items-center gap-1.5 rounded-md border border-blaze/30 bg-blaze/10 px-3.5 py-2 text-sm font-medium text-blaze transition-colors hover:bg-blaze/15 disabled:cursor-not-allowed disabled:opacity-40"
          >
            Run
            <CornerDownLeft className="h-3.5 w-3.5" />
          </button>
        </form>
      </div>
    </Card>
  );
}
