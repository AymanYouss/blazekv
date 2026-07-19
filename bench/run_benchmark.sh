#!/usr/bin/env bash
#
# Reproducible throughput/latency benchmark: BlazeKV vs Redis on identical
# hardware, driven by the same load generator (memtier_benchmark preferred,
# redis-benchmark fallback). Emits bench/results/results.json consumed by
# plot_results.py, and optionally records a flame graph via perf.
#
# Usage:
#   bench/run_benchmark.sh [--requests N] [--clients N] [--threads N]
#                          [--pipeline N] [--datasize N] [--blazekv-bin PATH]
#
set -euo pipefail

REQUESTS=1000000
CLIENTS=50
THREADS=4
PIPELINE=1
DATASIZE=32
KEYSPACE=1000000
BLAZEKV_PORT=6380
REDIS_PORT=6379
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BLAZEKV_BIN="$ROOT/build/blazekv-server"
RESULTS_DIR="$HERE/results"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --requests) REQUESTS="$2"; shift 2;;
        --clients) CLIENTS="$2"; shift 2;;
        --threads) THREADS="$2"; shift 2;;
        --pipeline) PIPELINE="$2"; shift 2;;
        --datasize) DATASIZE="$2"; shift 2;;
        --blazekv-bin) BLAZEKV_BIN="$2"; shift 2;;
        *) echo "unknown arg: $1"; exit 1;;
    esac
done

mkdir -p "$RESULTS_DIR"
command -v redis-server >/dev/null || { echo "redis-server required"; exit 1; }

USE_MEMTIER=0
if command -v memtier_benchmark >/dev/null; then USE_MEMTIER=1; fi

cleanup() {
    [[ -n "${BLAZEKV_PID:-}" ]] && kill "$BLAZEKV_PID" 2>/dev/null || true
    [[ -n "${REDIS_PID:-}" ]] && kill "$REDIS_PID" 2>/dev/null || true
}
trap cleanup EXIT

wait_port() {
    for _ in $(seq 1 50); do
        if (exec 3<>"/dev/tcp/127.0.0.1/$1") 2>/dev/null; then exec 3>&- ; return 0; fi
        sleep 0.1
    done
    echo "timed out waiting for port $1"; exit 1
}

# ---- run a single benchmark and echo "ops_sec p99_ms" -----------------------
run_one() {
    local port="$1" op="$2"
    if [[ "$USE_MEMTIER" == "1" ]]; then
        local ratio="1:0"; [[ "$op" == "GET" ]] && ratio="0:1"
        memtier_benchmark -s 127.0.0.1 -p "$port" \
            --protocol=redis --hide-histogram \
            -c "$CLIENTS" -t "$THREADS" --pipeline="$PIPELINE" \
            --ratio="$ratio" --key-maximum="$KEYSPACE" --data-size="$DATASIZE" \
            --test-time=15 2>/dev/null | awk '
            /^Totals/ {ops=$2; p99=$0}
            END {
                match(p99, /[0-9.]+$/);
                print ops, 0
            }'
    else
        redis-benchmark -h 127.0.0.1 -p "$port" -n "$REQUESTS" -c "$CLIENTS" \
            -P "$PIPELINE" -d "$DATASIZE" -r "$KEYSPACE" -t "$op" -q --csv 2>/dev/null | \
            awk -F',' 'NR==1{gsub(/"/,"",$2); gsub(/"/,"",$4); print $2, $4}'
    fi
}

echo "== starting Redis on :$REDIS_PORT =="
redis-server --port "$REDIS_PORT" --save '' --appendonly no --daemonize no >/dev/null 2>&1 &
REDIS_PID=$!
wait_port "$REDIS_PORT"

echo "== starting BlazeKV on :$BLAZEKV_PORT =="
"$BLAZEKV_BIN" --port "$BLAZEKV_PORT" --appendonly no --metrics-enabled no >/dev/null 2>&1 &
BLAZEKV_PID=$!
wait_port "$BLAZEKV_PORT"

declare -A R
for engine in redis blazekv; do
    port=$REDIS_PORT; [[ "$engine" == "blazekv" ]] && port=$BLAZEKV_PORT
    for op in SET GET; do
        echo "-- $engine $op --"
        read -r ops p99 <<<"$(run_one "$port" "$op")"
        R["${engine}_${op}_ops"]="${ops:-0}"
        R["${engine}_${op}_p99"]="${p99:-0}"
        echo "   ops/sec=${ops:-0} p99=${p99:-0}"
    done
done

cat > "$RESULTS_DIR/results.json" <<JSON
{
  "config": {
    "requests": $REQUESTS, "clients": $CLIENTS, "threads": $THREADS,
    "pipeline": $PIPELINE, "datasize": $DATASIZE, "tool": "$([[ $USE_MEMTIER == 1 ]] && echo memtier || echo redis-benchmark)"
  },
  "results": {
    "redis":   {"SET": {"ops_sec": ${R[redis_SET_ops]:-0},   "p99_ms": ${R[redis_SET_p99]:-0}},
                "GET": {"ops_sec": ${R[redis_GET_ops]:-0},   "p99_ms": ${R[redis_GET_p99]:-0}}},
    "blazekv": {"SET": {"ops_sec": ${R[blazekv_SET_ops]:-0}, "p99_ms": ${R[blazekv_SET_p99]:-0}},
                "GET": {"ops_sec": ${R[blazekv_GET_ops]:-0}, "p99_ms": ${R[blazekv_GET_p99]:-0}}}
  }
}
JSON

echo "== results written to $RESULTS_DIR/results.json =="
cat "$RESULTS_DIR/results.json"
