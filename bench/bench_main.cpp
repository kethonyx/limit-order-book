// Order book benchmark: replays a synthetic order flow and reports
//   - throughput (ops/sec), untimed per op, median of several runs
//   - per-operation latency percentiles, overall and per op type
//   - heap allocations per op (global operator new is counted)
//
//   lob_bench [num_ops] [runs] [reserve]
//
// reserve > 0 constructs the book with that capacity hint (see OrderBook).
//
// Build with the `release` preset; results from Debug builds are meaningless.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "lob/order_book.hpp"
#include "order_flow.hpp"

// --- Allocation counting ------------------------------------------------------
// Replacing the global operator new lets us count every heap allocation made by
// the book (std::list nodes, std::map nodes, unordered_map nodes, rehashes).

namespace {
std::uint64_t g_allocations = 0;
}  // namespace

void* operator new(std::size_t size) {
    ++g_allocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc{};
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// ------------------------------------------------------------------------------

namespace {

using namespace lob;
using namespace lob::bench;
using Clock = std::chrono::steady_clock;

// Keeps results observable so the optimizer cannot drop the work.
std::uint64_t g_sink = 0;
std::size_t g_reserve = 0;

inline void apply(OrderBook& book, const Op& op, std::vector<Trade>& trades) {
    trades.clear();
    if (op.kind == OpKind::Cancel) {
        g_sink += static_cast<std::uint64_t>(book.cancel(op.order.id));
    } else {
        g_sink += static_cast<std::uint64_t>(book.add(op.order, trades).filled);
    }
}

struct ThroughputRun {
    double seconds;
    std::uint64_t allocations;
    std::size_t final_orders;
};

ThroughputRun run_throughput(const std::vector<Op>& ops) {
    OrderBook book(g_reserve);
    std::vector<Trade> trades;
    trades.reserve(1024);

    const std::uint64_t allocs_before = g_allocations;
    const auto start = Clock::now();
    for (const Op& op : ops) apply(book, op, trades);
    const auto end = Clock::now();

    return {std::chrono::duration<double>(end - start).count(), g_allocations - allocs_before, book.order_count()};
}

std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, double p) {
    if (sorted.empty()) return 0;
    const auto idx = static_cast<std::size_t>(p / 100.0 * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

void print_latency_row(const char* name, std::vector<std::uint64_t>& ns) {
    std::sort(ns.begin(), ns.end());
    std::printf("  %-8s %10zu %8llu %8llu %8llu %8llu %10llu\n", name, ns.size(),
                static_cast<unsigned long long>(percentile(ns, 50)),
                static_cast<unsigned long long>(percentile(ns, 90)),
                static_cast<unsigned long long>(percentile(ns, 99)),
                static_cast<unsigned long long>(percentile(ns, 99.9)),
                static_cast<unsigned long long>(ns.empty() ? 0 : ns.back()));
}

void run_latency(const std::vector<Op>& ops) {
    OrderBook book(g_reserve);
    std::vector<Trade> trades;
    trades.reserve(1024);

    std::array<std::vector<std::uint64_t>, 3> by_kind;
    for (auto& v : by_kind) v.reserve(ops.size());
    std::vector<std::uint64_t> all;
    all.reserve(ops.size());

    for (const Op& op : ops) {
        const auto t0 = Clock::now();
        apply(book, op, trades);
        const auto t1 = Clock::now();
        const auto ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        all.push_back(ns);
        by_kind[static_cast<std::size_t>(op.kind)].push_back(ns);
    }

    std::printf("\nLatency per operation (ns, includes timer overhead):\n");
    std::printf("  %-8s %10s %8s %8s %8s %8s %10s\n", "op", "count", "p50", "p90", "p99", "p99.9", "max");
    print_latency_row("all", all);
    print_latency_row("limit", by_kind[0]);
    print_latency_row("market", by_kind[1]);
    print_latency_row("cancel", by_kind[2]);
}

// Smallest non-zero step the clock reports, and the cost of an empty
// now()/now() pair. Both bound how precise the latency numbers can be.
void print_timer_info() {
    std::uint64_t min_step = UINT64_MAX;
    std::vector<std::uint64_t> empty;
    empty.reserve(100'000);
    for (int i = 0; i < 100'000; ++i) {
        const auto a = Clock::now();
        const auto b = Clock::now();
        const auto d = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
        empty.push_back(d);
        if (d > 0) min_step = std::min(min_step, d);
    }
    std::sort(empty.begin(), empty.end());
    std::printf("Timer: steady_clock resolution ~%llu ns, empty-measurement p50 %llu ns\n",
                static_cast<unsigned long long>(min_step), static_cast<unsigned long long>(percentile(empty, 50)));
}

}  // namespace

int main(int argc, char** argv) {
#ifndef NDEBUG
    std::printf("WARNING: assertions enabled (not a Release build); numbers are not representative.\n");
#endif
    FlowConfig cfg;
    int runs = 5;
    if (argc > 1) cfg.num_ops = std::stoull(argv[1]);
    if (argc > 2) runs = std::stoi(argv[2]);
    if (argc > 3) g_reserve = std::stoull(argv[3]);

    const std::vector<Op> ops = generate_flow(cfg);
    std::size_t counts[3] = {};
    for (const Op& op : ops) ++counts[static_cast<std::size_t>(op.kind)];

    std::printf("Order flow: %zu ops (limit %zu, market %zu, cancel %zu), seed %llu\n", ops.size(), counts[0],
                counts[1], counts[2], static_cast<unsigned long long>(cfg.seed));
    print_timer_info();
    std::printf("Index capacity hint: %zu\n", g_reserve);

    run_throughput(ops);  // warm-up: page in memory, warm caches and branch predictors

    std::vector<ThroughputRun> results;
    for (int i = 0; i < runs; ++i) results.push_back(run_throughput(ops));
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.seconds < b.seconds; });

    const auto ops_per_sec = [&](const ThroughputRun& r) { return static_cast<double>(ops.size()) / r.seconds; };
    const ThroughputRun& median = results[results.size() / 2];

    std::printf("\nThroughput over %d runs (ops/sec): median %.0f, best %.0f, worst %.0f\n", runs,
                ops_per_sec(median), ops_per_sec(results.front()), ops_per_sec(results.back()));
    std::printf("Mean time per op (median run): %.1f ns\n", median.seconds * 1e9 / static_cast<double>(ops.size()));
    std::printf("Heap allocations: %.3f per op; resting orders at end: %zu\n",
                static_cast<double>(median.allocations) / static_cast<double>(ops.size()), median.final_orders);

    run_latency(ops);

    std::printf("\n(checksum %llu)\n", static_cast<unsigned long long>(g_sink));
    return 0;
}
