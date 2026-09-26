// Differential test: replays random order flow through OrderBook and through
// a deliberately naive reference model (a flat vector, linear scans), and
// requires identical trades and top of book after every operation.
// The reference is too slow for production but easy to verify by eye.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "lob/order_book.hpp"

namespace lob {
namespace {

class ReferenceBook {
public:
    ExecResult add(Order in, std::vector<Trade>& trades) {
        if (in.qty <= 0) return {.status = Status::InvalidQty};
        if (in.type == OrderType::Limit && in.price <= 0) return {.status = Status::InvalidPrice};
        if (find(in.id) != resting_.end()) return {.status = Status::DuplicateId};

        ExecResult r;
        while (in.qty > 0) {
            auto best = best_opposite(in);
            if (best == resting_.end()) break;
            const Qty q = std::min(in.qty, best->order.qty);
            trades.push_back(in.side == Side::Buy ? Trade{in.id, best->order.id, best->order.price, q}
                                                  : Trade{best->order.id, in.id, best->order.price, q});
            in.qty -= q;
            best->order.qty -= q;
            r.filled += q;
            if (best->order.qty == 0) resting_.erase(best);
        }
        if (in.type == OrderType::Market) {
            r.cancelled = in.qty;
        } else if (in.qty > 0) {
            resting_.push_back({in, seq_++});
            r.rested = in.qty;
        }
        return r;
    }

    Status cancel(OrderId id) {
        const auto it = find(id);
        if (it == resting_.end()) return Status::UnknownId;
        resting_.erase(it);
        return Status::Accepted;
    }

    std::optional<Price> best(Side side) const {
        std::optional<Price> out;
        for (const auto& e : resting_) {
            if (e.order.side != side) continue;
            if (!out || (side == Side::Buy ? e.order.price > *out : e.order.price < *out)) out = e.order.price;
        }
        return out;
    }

    std::size_t size() const { return resting_.size(); }

private:
    struct Entry {
        Order order;
        std::uint64_t seq;
    };

    std::vector<Entry>::iterator find(OrderId id) {
        return std::ranges::find_if(resting_, [id](const Entry& e) { return e.order.id == id; });
    }

    // Best price for the incoming order, earliest arrival among equals.
    std::vector<Entry>::iterator best_opposite(const Order& in) {
        auto best = resting_.end();
        for (auto it = resting_.begin(); it != resting_.end(); ++it) {
            const Order& o = it->order;
            if (o.side == in.side) continue;
            const bool crosses = in.type == OrderType::Market ||
                                 (in.side == Side::Buy ? o.price <= in.price : o.price >= in.price);
            if (!crosses) continue;
            if (best == resting_.end()) {
                best = it;
                continue;
            }
            const Price bp = best->order.price;
            const bool better = in.side == Side::Buy ? o.price < bp : o.price > bp;
            if (better || (o.price == bp && it->seq < best->seq)) best = it;
        }
        return best;
    }

    std::vector<Entry> resting_;
    std::uint64_t seq_ = 0;
};

class Randomized : public ::testing::TestWithParam<std::uint64_t> {};

TEST_P(Randomized, MatchesReferenceModel) {
    std::mt19937_64 rng(GetParam());
    auto pick = [&](std::int64_t lo, std::int64_t hi) {
        return std::uniform_int_distribution<std::int64_t>(lo, hi)(rng);
    };

    OrderBook book;
    ReferenceBook ref;
    std::vector<Trade> got;
    std::vector<Trade> want;
    std::vector<OrderId> issued;

    for (int step = 0; step < 5'000; ++step) {
        got.clear();
        want.clear();
        const auto action = pick(0, 99);

        if (action < 25 && !issued.empty()) {
            // Cancel a random previously issued id: may be live, filled or cancelled.
            const OrderId id = issued[static_cast<std::size_t>(pick(0, static_cast<std::int64_t>(issued.size()) - 1))];
            ASSERT_EQ(book.cancel(id), ref.cancel(id)) << "step " << step;
        } else {
            // Ids occasionally collide on purpose to exercise DuplicateId;
            // quantities occasionally invalid.
            const auto id = static_cast<OrderId>(pick(1, 2'000));
            const Order o{
                .id = id,
                .side = pick(0, 1) == 0 ? Side::Buy : Side::Sell,
                .type = pick(0, 9) == 0 ? OrderType::Market : OrderType::Limit,
                .price = pick(95, 105),  // narrow band -> frequent crossing
                .qty = pick(-1, 50),
            };
            issued.push_back(id);
            ASSERT_EQ(book.add(o, got), ref.add(o, want)) << "step " << step;
        }

        ASSERT_EQ(got, want) << "step " << step;
        ASSERT_EQ(book.best_bid(), ref.best(Side::Buy)) << "step " << step;
        ASSERT_EQ(book.best_ask(), ref.best(Side::Sell)) << "step " << step;
        ASSERT_EQ(book.order_count(), ref.size()) << "step " << step;
        if (const auto bid = book.best_bid(), ask = book.best_ask(); bid && ask) {
            ASSERT_LT(*bid, *ask) << "book crossed at step " << step;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Seeds, Randomized, ::testing::Values(1u, 2u, 3u, 42u, 2026u));

}  // namespace
}  // namespace lob
