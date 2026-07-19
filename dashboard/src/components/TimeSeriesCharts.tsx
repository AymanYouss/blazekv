import {
  Area,
  AreaChart,
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import { Card } from './Card';
import { ChartTooltip } from './ChartTooltip';
import { clockLabel, formatBytes, formatCompact, formatLatency } from '@/lib/format';
import type { Sample } from '@/types';

interface Props {
  series: Sample[];
}

const AXIS = { stroke: 'transparent', fontSize: 10 };
const TICK = { fill: '#64748b', fontSize: 10, fontFamily: 'JetBrains Mono, monospace' };
const MARGIN = { top: 8, right: 12, bottom: 4, left: 4 };

function xAxis() {
  return (
    <XAxis
      dataKey="t"
      tickFormatter={(t: number) => clockLabel(t).slice(0, 5)}
      tick={TICK}
      axisLine={AXIS}
      tickLine={false}
      minTickGap={48}
      interval="preserveStartEnd"
    />
  );
}

function bytesLabel(n: number): string {
  const b = formatBytes(n);
  return `${b.value} ${b.unit}`;
}

function latencyLabel(n: number): string {
  const l = formatLatency(n);
  return `${l.value}${l.unit}`;
}

export function TimeSeriesCharts({ series }: Props) {
  return (
    <div className="grid grid-cols-1 gap-3 lg:grid-cols-3">
      <Card title="Throughput" subtitle="operations / second" className="pb-2">
        <div className="h-[188px] px-1 pt-2">
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={series} margin={MARGIN}>
              <defs>
                <linearGradient id="thr" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor="#F5A623" stopOpacity="0.32" />
                  <stop offset="100%" stopColor="#F5A623" stopOpacity="0" />
                </linearGradient>
              </defs>
              <CartesianGrid vertical={false} />
              {xAxis()}
              <YAxis
                tick={TICK}
                axisLine={AXIS}
                tickLine={false}
                width={38}
                tickFormatter={(v: number) => formatCompact(v, 0)}
              />
              <Tooltip content={<ChartTooltip format={(v) => `${formatCompact(v)} ops/s`} />} />
              <Area
                type="monotone"
                dataKey="throughput"
                name="Throughput"
                stroke="#F5A623"
                strokeWidth={1.75}
                fill="url(#thr)"
                isAnimationActive={false}
                dot={false}
              />
            </AreaChart>
          </ResponsiveContainer>
        </div>
      </Card>

      <Card title="Latency" subtitle="p50 / p99 / p999" className="pb-2">
        <div className="h-[188px] px-1 pt-2">
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={series} margin={MARGIN}>
              <CartesianGrid vertical={false} />
              {xAxis()}
              <YAxis
                tick={TICK}
                axisLine={AXIS}
                tickLine={false}
                width={40}
                tickFormatter={latencyLabel}
              />
              <Tooltip content={<ChartTooltip format={latencyLabel} />} />
              <Line
                type="monotone"
                dataKey="p50_us"
                name="p50"
                stroke="#38bdf8"
                strokeWidth={1.5}
                dot={false}
                isAnimationActive={false}
              />
              <Line
                type="monotone"
                dataKey="p99_us"
                name="p99"
                stroke="#F5A623"
                strokeWidth={1.75}
                dot={false}
                isAnimationActive={false}
              />
              <Line
                type="monotone"
                dataKey="p999_us"
                name="p999"
                stroke="#f472b6"
                strokeWidth={1.25}
                strokeDasharray="3 3"
                dot={false}
                isAnimationActive={false}
              />
            </LineChart>
          </ResponsiveContainer>
        </div>
      </Card>

      <Card title="Memory" subtitle="resident set" className="pb-2">
        <div className="h-[188px] px-1 pt-2">
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={series} margin={MARGIN}>
              <defs>
                <linearGradient id="mem" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor="#4ADE80" stopOpacity="0.24" />
                  <stop offset="100%" stopColor="#4ADE80" stopOpacity="0" />
                </linearGradient>
              </defs>
              <CartesianGrid vertical={false} />
              {xAxis()}
              <YAxis
                tick={TICK}
                axisLine={AXIS}
                tickLine={false}
                width={52}
                domain={['dataMin', 'dataMax']}
                tickFormatter={bytesLabel}
              />
              <Tooltip content={<ChartTooltip format={bytesLabel} />} />
              <Area
                type="monotone"
                dataKey="memory_bytes"
                name="Memory"
                stroke="#4ADE80"
                strokeWidth={1.5}
                fill="url(#mem)"
                isAnimationActive={false}
                dot={false}
              />
            </AreaChart>
          </ResponsiveContainer>
        </div>
      </Card>
    </div>
  );
}
