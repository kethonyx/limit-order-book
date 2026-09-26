#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "lob/object_pool.hpp"
#include "lob/types.hpp"

namespace lob {

// Outcome of submitting an order. For an accepted order,
// filled + rested + cancelled == original quantity.
struct ExecResult {
    Status status{Status::Accepted};
    Qty filled{};     // quantity executed against resting orders
    Qty rested{};     // quantity left resting in the book (limit orders only)
    Qty cancelled{};  // unfilled quantity discarded (market orders only)

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
//   Level : intrusive doubly-linked FIFO of resting orders at that price
//   index_: OrderId -> node, for O(1) lookup on cancel
//   pool_ : owns all order nodes, so resting an order does not call malloc
//
// Not copyable: nodes hold pointers into this book's own levels.
class OrderBook {
public:
    OrderBook() = default;

    // Optional capacity hint: pre-sizes the id index for this many live orders
    // so it never rehashes below that size. A rehash moves every entry at once
    // and shows up as a ~1 ms latency outlier at ~100k resting orders.
    explicit OrderBook(std::size_t expected_orders) { index_.reserve(expected_orders); }
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = default;
    OrderBook& operator=(OrderBook&&) = default;
    ~OrderBook() = default;

    // Submits an order. Trades are appended to `trades` (not cleared), so the
    // caller can reuse one buffer across calls and avoid reallocations.
    //
    // Market orders are immediate-or-cancel: they sweep the opposite side at
    // any price until filled or the side is empty; the remainder is cancelled
    // (reported in ExecResult::cancelled), never rested.
    ExecResult add(const Order& order, std::vector<Trade>& trades);

    // Removes a resting order. UnknownId if it never existed, was already
    // cancelled, or was fully filled (filled orders leave the book).
    Status cancel(OrderId id);

    // Changes a resting limit order's price and/or quantity.
    //  - same price, smaller qty: reduced in place, keeps time priority
    //  - anything else: cancel + re-add, loses time priority and may trade
    // The id stays the same. UnknownId if not resting; InvalidQty/InvalidPrice
    // for bad new values (the original order is then left untouched).
    ExecResult modify(OrderId id, Price new_price, Qty new_qty, std::vector<Trade>& trades);

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
    struct Level;

    // One resting order. prev/next link it into its level's FIFO; `level`
    // lets cancel find the level without a map lookup.
    struct OrderNode {
        Order order;
        OrderNode* prev = nullptr;
        OrderNode* next = nullptr;
        Level* level = nullptr;
    };

    struct Level {
        Qty total_qty{};
        std::size_t count{};
        OrderNode* head = nullptr;  // oldest = first to fill
        OrderNode* tail = nullptr;

        [[nodiscard]] bool empty() const noexcept { return head == nullptr; }
        void push_back(OrderNode* node) noexcept;
        void unlink(OrderNode* node) noexcept;
    };

    // std::map nodes never move, so Level* stored in OrderNode stays valid
    // until that level is erased (which only happens once it is empty).
    using Bids = std::map<Price, Level, std::greater<>>;
    using Asks = std::map<Price, Level, std::less<>>;

    Status validate(const Order& order) const;

    // Matches `incoming` against the opposite side while `crosses(level_price)`
    // holds. Decrements incoming.qty, emits trades, returns filled quantity.
    template <class Levels, class Crosses>
    Qty match(Levels& levels, Order& incoming, Crosses crosses, std::vector<Trade>& trades);

    template <class Levels>
    void rest(Levels& levels, const Order& order);

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, OrderNode*> index_;
    ObjectPool<OrderNode> pool_;
};

}  // namespace lob
