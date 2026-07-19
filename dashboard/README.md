# BlazeKV Console

A real-time observability dashboard for **BlazeKV**, a Redis-compatible in-memory database built on a thread-per-core, shared-nothing architecture. It polls the server once per second, keeps a rolling in-browser time series, and renders throughput, tail-latency percentiles, memory, per-shard load balance, and an interactive query console. When the server is unreachable it transparently falls back to synthesized demo data so the view stays alive.

## Running

```bash
pnpm install
pnpm dev
```

The dev server proxies `/api/*` and `/metrics` to `http://localhost:9121`, so start BlazeKV on port 9121 first for live data. Point the proxy elsewhere with `BLAZEKV_URL=http://host:port pnpm dev`. With no server running, the dashboard renders realistic demo data automatically.

## Scripts

| Script            | Purpose                                  |
| ----------------- | ---------------------------------------- |
| `pnpm dev`        | Start the Vite dev server (proxied).     |
| `pnpm build`      | Type-check, then produce a static build. |
| `pnpm preview`    | Serve the production build locally.      |
| `pnpm typecheck`  | Run `tsc --noEmit`.                      |

## Stack

Vite · React · TypeScript · Tailwind CSS · Recharts · lucide-react.
