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
    const bool is_market = incoming.type == OrderType::Market;
    ExecResult result;

    if (incoming.side == Side::Buy) {
        // A buy crosses any ask priced at or below its limit (a market buy: any ask).
        result.filled =
            match(asks_, incoming, [&](Price ask) { return is_market || ask <= incoming.price; }, trades);
        if (incoming.qty > 0 && !is_market) rest(bids_, incoming);
    } else {
        // A sell crosses any bid priced at or above its limit (a market sell: any bid).
        result.filled =
            match(bids_, incoming, [&](Price bid) { return is_market || bid >= incoming.price; }, trades);
        if (incoming.qty > 0 && !is_market) rest(asks_, incoming);
    }

    if (is_market) {
        result.cancelled = incoming.qty;
    } else {
        result.rested = incoming.qty;
    }
    return result;
}

Status OrderBook::cancel(OrderId id) {
    const auto found = index_.find(id);
    if (found == index_.end()) return Status::UnknownId;

    auto [level, order_it] = found->second;
    const Side side = order_it->side;
    const Price price = order_it->price;

    level->total_qty -= order_it->qty;
    level->orders.erase(order_it);  // O(1): we hold the iterator
    index_.erase(found);

    // Empty levels are removed so best_bid/best_ask stay O(1) via begin().
    if (level->orders.empty()) {
        if (side == Side::Buy) {
            bids_.erase(price);
        } else {
            asks_.erase(price);
        }
    }
    return Status::Accepted;
}

ExecResult OrderBook::modify(OrderId id, Price new_price, Qty new_qty, std::vector<Trade>& trades) {
    const auto found = index_.find(id);
    if (found == index_.end()) return {.status = Status::UnknownId};
    if (new_qty <= 0) return {.status = Status::InvalidQty};
    if (new_price <= 0) return {.status = Status::InvalidPrice};

    auto [level, order_it] = found->second;

    // Reducing size at the same price does not disadvantage anyone queued
    // behind, so the order keeps its place.
    if (new_price == order_it->price && new_qty <= order_it->qty) {
        level->total_qty -= order_it->qty - new_qty;
        order_it->qty = new_qty;
        return {.status = Status::Accepted, .rested = new_qty};
    }

    // Price change or size increase: goes to the back of the queue.
    Order replacement = *order_it;
    replacement.price = new_price;
    replacement.qty = new_qty;
    cancel(id);
    return add(replacement, trades);
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
