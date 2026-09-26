#include "lob/order_book.hpp"

#include <algorithm>

namespace lob {

Status OrderBook::validate(const Order& order) const {
    if (order.qty <= 0) return Status::InvalidQty;
    if (order.type == OrderType::Limit && order.price <= 0) return Status::InvalidPrice;
    if (index_.contains(order.id)) return Status::DuplicateId;
    return Status::Accepted;
}

ExecResult OrderBook::add(const Order& order, std::vector<Trade>& trades) {
    if (const Status s = validate(order); s != Status::Accepted) return {.status = s};

    Order incoming = order;
    ExecResult result;

    if (incoming.side == Side::Buy) {
        // A buy crosses any ask priced at or below its limit.
        result.filled = match(asks_, incoming, [&](Price ask) { return ask <= incoming.price; }, trades);
        if (incoming.qty > 0) rest(bids_, incoming);
    } else {
        // A sell crosses any bid priced at or above its limit.
        result.filled = match(bids_, incoming, [&](Price bid) { return bid >= incoming.price; }, trades);
        if (incoming.qty > 0) rest(asks_, incoming);
    }
    result.rested = incoming.qty;
    return result;
}

template <class Levels, class Crosses>
Qty OrderBook::match(Levels& levels, Order& incoming, Crosses crosses, std::vector<Trade>& trades) {
    Qty filled = 0;

    // Walk levels from best price outward; stop when the book no longer crosses.
    while (incoming.qty > 0 && !levels.empty()) {
        auto level_it = levels.begin();
        const Price level_price = level_it->first;
        if (!crosses(level_price)) break;

        Level& level = level_it->second;
        // Within a level, fill oldest orders first (time priority).
        while (incoming.qty > 0 && !level.orders.empty()) {
            Order& resting = level.orders.front();
            const Qty q = std::min(incoming.qty, resting.qty);

            // Trade executes at the resting order's price: the resting order
            // set the price, the incoming order accepted it.
            if (incoming.side == Side::Buy) {
                trades.push_back({.buy_id = incoming.id, .sell_id = resting.id, .price = level_price, .qty = q});
            } else {
                trades.push_back({.buy_id = resting.id, .sell_id = incoming.id, .price = level_price, .qty = q});
            }

            incoming.qty -= q;
            resting.qty -= q;
            level.total_qty -= q;
            filled += q;

            if (resting.qty == 0) {
                index_.erase(resting.id);
                level.orders.pop_front();
            }
        }

        if (level.orders.empty()) levels.erase(level_it);
    }
    return filled;
}

template <class Levels>
void OrderBook::rest(Levels& levels, const Order& order) {
    // operator[] creates the level if missing: O(log L) either way.
    Level& level = levels[order.price];
    level.orders.push_back(order);
    level.total_qty += order.qty;
    index_.emplace(order.id, Locator{&level, std::prev(level.orders.end())});
}

std::optional<Price> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Qty OrderBook::volume_at(Side side, Price price) const {
    auto lookup = [price](const auto& levels) -> Qty {
        const auto it = levels.find(price);
        return it == levels.end() ? 0 : it->second.total_qty;
    };
    return side == Side::Buy ? lookup(bids_) : lookup(asks_);
}

std::vector<LevelView> OrderBook::depth(Side side, std::size_t max_levels) const {
    auto collect = [max_levels](const auto& levels) {
        std::vector<LevelView> out;
        out.reserve(std::min(max_levels, levels.size()));
        for (const auto& [price, level] : levels) {
            if (out.size() == max_levels) break;
            out.push_back({.price = price, .qty = level.total_qty, .orders = level.orders.size()});
        }
        return out;
    };
    return side == Side::Buy ? collect(bids_) : collect(asks_);
}

}  // namespace lob
