#pragma once

// Deterministic synthetic order flow for benchmarks. Generated up front so
// the RNG never runs inside a timed region.

#include <cstdint>
#include <random>
#include <vector>

#include "lob/types.hpp"

namespace lob::bench {

enum class OpKind : std::uint8_t { Limit, Market, Cancel };

struct Op {
    OpKind kind;
    Order order;  // for Cancel only order.id is used
};

struct FlowConfig {
    std::size_t num_ops = 1'000'000;
    std::uint64_t seed = 42;
    Price mid = 10'000;  // $100.00 at 0.01 tick
    // Percentages; the remainder are cancels.
    int limit_pct = 55;
    int market_pct = 10;
};

// Mix:
//  - limit orders priced around a fixed mid: offset uniform in [-10, 40]
//    ticks away from mid on the passive side, so most rest (offset > 0) and
//    some cross the spread (offset <= 0);
//  - market orders of 1..100;
//  - cancels of a random earlier limit order (each id at most once); some of
//    those will already be filled and return UNKNOWN_ID, as in real flow.
inline std::vector<Op> generate_flow(const FlowConfig& cfg) {
    std::mt19937_64 rng(cfg.seed);
    std::uniform_int_distribution<int> pct(0, 99);
    std::uniform_int_distribution<int> side(0, 1);
    std::uniform_int_distribution<Price> offset(-10, 40);
    std::uniform_int_distribution<Qty> qty(1, 100);

    std::vector<Op> ops;
    ops.reserve(cfg.num_ops);
    std::vector<OrderId> cancellable;
    OrderId next_id = 1;

    while (ops.size() < cfg.num_ops) {
        const int roll = pct(rng);
        const Side s = side(rng) == 0 ? Side::Buy : Side::Sell;

        if (roll < cfg.limit_pct) {
            const Price off = offset(rng);
            const Price price = s == Side::Buy ? cfg.mid - off : cfg.mid + off;
            ops.push_back({OpKind::Limit, {.id = next_id, .side = s, .type = OrderType::Limit, .price = price, .qty = qty(rng)}});
            cancellable.push_back(next_id++);
        } else if (roll < cfg.limit_pct + cfg.market_pct) {
            ops.push_back({OpKind::Market, {.id = next_id++, .side = s, .type = OrderType::Market, .price = 0, .qty = qty(rng)}});
        } else if (!cancellable.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, cancellable.size() - 1);
            const std::size_t i = pick(rng);
            ops.push_back({OpKind::Cancel, {.id = cancellable[i]}});
            cancellable[i] = cancellable.back();  // swap-remove: O(1)
            cancellable.pop_back();
        }
    }
    return ops;
}

}  // namespace lob::bench
