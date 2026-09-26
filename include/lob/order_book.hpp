#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "lob/types.hpp"

namespace lob {

// Outcome of submitting an order. filled + rested == original qty for an
// accepted limit order.
struct ExecResult {
    Status status{Status::Accepted};
    Qty filled{};  // quantity executed against resting orders
    Qty rested{};  // quantity left resting in the book (limit orders)

    friend bool operator==(const ExecResult&, const ExecResult&) = default;
};

// Aggregated view of one price level, used for printing / tests.
struct LevelView {
    Price price{};
    Qty qty{};
    std::size_t orders{};

    friend bool operator==(const LevelView&, const LevelView&) = default;
};

// Single-instrument, single-threaded limit order book with price-time priority.
//
//   bids_ : price -> Level, best (highest) price first
//   asks_ : price -> Level, best (lowest) price first
//   Level : FIFO list of resting orders at that price
//   index_: OrderId -> position in its level, for O(1) lookup on cancel
class OrderBook {
public:
    // Submits an order. Trades are appended to `trades` (not cleared), so the
    // caller can reuse one buffer across calls and avoid reallocations.
    ExecResult add(const Order& order, std::vector<Trade>& trades);

    [[nodiscard]] std::optional<Price> best_bid() const;
    [[nodiscard]] std::optional<Price> best_ask() const;

    // Total resting quantity at a price (0 if the level does not exist).
    [[nodiscard]] Qty volume_at(Side side, Price price) const;

    // Up to `max_levels` levels on one side, best price first.
    [[nodiscard]] std::vector<LevelView> depth(Side side, std::size_t max_levels) const;

    [[nodiscard]] bool contains(OrderId id) const { return index_.contains(id); }
    [[nodiscard]] std::size_t order_count() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t level_count(Side side) const noexcept {
        return side == Side::Buy ? bids_.size() : asks_.size();
    }

private:
    using OrderList = std::list<Order>;

    struct Level {
        Qty total_qty{};
        OrderList orders;  // front = oldest = first to fill
    };

    using Bids = std::map<Price, Level, std::greater<>>;
    using Asks = std::map<Price, Level, std::less<>>;

    // std::list iterators stay valid until that element is erased, so they can
    // be stored and used later for O(1) removal.
    struct Locator {
        Level* level;
        OrderList::iterator it;
    };

    Status validate(const Order& order) const;

    // Matches `incoming` against the opposite side while `crosses(level_price)`
    // holds. Decrements incoming.qty, emits trades, returns filled quantity.
    template <class Levels, class Crosses>
    Qty match(Levels& levels, Order& incoming, Crosses crosses, std::vector<Trade>& trades);

    template <class Levels>
    void rest(Levels& levels, const Order& order);

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Locator> index_;
};

}  // namespace lob
