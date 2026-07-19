// Microbenchmarks for BlazeKV's hot-path data structures, measured with Google
// Benchmark. These isolate the components that drive GET/SET throughput from
// networking so regressions in the core structures are caught directly.
#include <benchmark/benchmark.h>

#include <random>
#include <string>
#include <vector>

#include "blazekv/hyperloglog.hpp"
#include "blazekv/resp.hpp"
#include "blazekv/skiplist.hpp"
#include "blazekv/swiss_table.hpp"

using namespace blazekv;

namespace {
std::vector<std::string> make_keys(std::size_t n) {
    std::vector<std::string> keys;
    keys.reserve(n);
    std::mt19937_64 rng(42);
    for (std::size_t i = 0; i < n; ++i) keys.push_back("key:" + std::to_string(rng()));
    return keys;
}
}  // namespace

static void BM_SwissTable_Insert(benchmark::State& state) {
    const auto keys = make_keys(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        SwissTable<std::string, std::string> t;
        for (const auto& k : keys) t.insert_or_assign(k, k);
        benchmark::DoNotOptimize(t.size());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_SwissTable_Insert)->Arg(1000)->Arg(100000);

static void BM_SwissTable_Get(benchmark::State& state) {
    const auto keys = make_keys(static_cast<std::size_t>(state.range(0)));
    SwissTable<std::string, std::string> t;
    for (const auto& k : keys) t.insert_or_assign(k, k);
    std::size_t i = 0;
    for (auto _ : state) {
        auto* v = t.find(keys[i++ % keys.size()]);
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SwissTable_Get)->Arg(1000)->Arg(100000);

static void BM_SwissTable_GetMiss(benchmark::State& state) {
    const auto keys = make_keys(100000);
    SwissTable<std::string, std::string> t;
    for (const auto& k : keys) t.insert_or_assign(k, k);
    const auto misses = make_keys(1000);  // different seed sequence, mostly absent
    std::size_t i = 0;
    for (auto _ : state) {
        auto* v = t.find("absent:" + misses[i++ % misses.size()]);
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SwissTable_GetMiss);

static void BM_SkipList_Insert(benchmark::State& state) {
    std::mt19937_64 rng(7);
    for (auto _ : state) {
        SkipList sl;
        for (int i = 0; i < state.range(0); ++i)
            sl.insert("m" + std::to_string(i), static_cast<double>(rng() % 1000000));
        benchmark::DoNotOptimize(sl.size());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_SkipList_Insert)->Arg(10000);

static void BM_Resp_ParseMultibulk(benchmark::State& state) {
    std::string in = "*3\r\n$3\r\nSET\r\n$16\r\nkey:0123456789ab\r\n$5\r\nvalue\r\n";
    RequestParser p;
    for (auto _ : state) {
        Command cmd;
        std::size_t consumed = 0;
        auto st = p.parse(in.data(), in.size(), cmd, consumed);
        benchmark::DoNotOptimize(st);
        benchmark::DoNotOptimize(cmd.argv.size());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Resp_ParseMultibulk);

static void BM_Hll_Add(benchmark::State& state) {
    std::string regs;
    std::mt19937_64 rng(1);
    for (auto _ : state) {
        hll::add(regs, "e:" + std::to_string(rng()));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Hll_Add);

BENCHMARK_MAIN();
