#include "lob/order_book.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace lob {
namespace {

Order limit(OrderId id, Side side, Price price, Qty qty) {
    return {.id = id, .side = side, .type = OrderType::Limit, .price = price, .qty = qty};
}

class OrderBookTest : public ::testing::Test {
protected:
    ExecResult add(const Order& o) { return book.add(o, trades); }

    OrderBook book;
    std::vector<Trade> trades;
};

TEST_F(OrderBookTest, EmptyBookHasNoBestPrices) {
    EXPECT_FALSE(book.best_bid());
    EXPECT_FALSE(book.best_ask());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(OrderBookTest, NonCrossingOrdersRest) {
    EXPECT_EQ(add(limit(1, Side::Buy, 100, 10)), (ExecResult{.status = Status::Accepted, .filled = 0, .rested = 10}));
    add(limit(2, Side::Sell, 101, 5));

    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 101);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.order_count(), 2u);
}

TEST_F(OrderBookTest, BestBidIsHighestBestAskIsLowest) {
    add(limit(1, Side::Buy, 99, 1));
    add(limit(2, Side::Buy, 100, 1));
    add(limit(3, Side::Sell, 103, 1));
    add(limit(4, Side::Sell, 102, 1));

    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 102);
}

TEST_F(OrderBookTest, FullFillAtRestingPrice) {
    add(limit(1, Side::Sell, 100, 10));
    const auto r = add(limit(2, Side::Buy, 105, 10));

    EXPECT_EQ(r.filled, 10);
    EXPECT_EQ(r.rested, 0);
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0], (Trade{.buy_id = 2, .sell_id = 1, .price = 100, .qty = 10}));
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_ask());
}

TEST_F(OrderBookTest, AggressorRemainderRests) {
    add(limit(1, Side::Buy, 100, 4));
    const auto r = add(limit(2, Side::Sell, 100, 10));

    EXPECT_EQ(r.filled, 4);
    EXPECT_EQ(r.rested, 6);
    EXPECT_FALSE(book.best_bid());
    EXPECT_EQ(book.best_ask(), 100);
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 6);
}

TEST_F(OrderBookTest, RejectsInvalidOrders) {
    EXPECT_EQ(add(limit(1, Side::Buy, 100, 0)).status, Status::InvalidQty);
    EXPECT_EQ(add(limit(2, Side::Buy, 0, 5)).status, Status::InvalidPrice);
    add(limit(3, Side::Buy, 100, 5));
    EXPECT_EQ(add(limit(3, Side::Sell, 200, 5)).status, Status::DuplicateId);
    EXPECT_EQ(book.order_count(), 1u);
}

Order market(OrderId id, Side side, Qty qty) {
    return {.id = id, .side = side, .type = OrderType::Market, .price = 0, .qty = qty};
}

TEST_F(OrderBookTest, MarketOrderFillsAtAnyPrice) {
    add(limit(1, Side::Sell, 100, 5));
    add(limit(2, Side::Sell, 150, 5));
    const auto r = add(market(3, Side::Buy, 10));

    EXPECT_EQ(r, (ExecResult{.status = Status::Accepted, .filled = 10, .rested = 0, .cancelled = 0}));
    EXPECT_FALSE(book.best_ask());
}

TEST_F(OrderBookTest, MarketOrderRemainderIsCancelledNotRested) {
    add(limit(1, Side::Buy, 100, 4));
    const auto r = add(market(2, Side::Sell, 10));

    EXPECT_EQ(r, (ExecResult{.status = Status::Accepted, .filled = 4, .rested = 0, .cancelled = 6}));
    EXPECT_FALSE(book.best_ask());
    EXPECT_FALSE(book.contains(2));
}

TEST_F(OrderBookTest, CancelRemovesOrderAndEmptyLevel) {
    add(limit(1, Side::Buy, 100, 5));
    EXPECT_EQ(book.cancel(1), Status::Accepted);
    EXPECT_FALSE(book.best_bid());
    EXPECT_EQ(book.level_count(Side::Buy), 0u);
    EXPECT_EQ(book.cancel(1), Status::UnknownId);
}

TEST_F(OrderBookTest, ModifyReduceKeepsPriority) {
    add(limit(1, Side::Sell, 100, 10));
    add(limit(2, Side::Sell, 100, 10));
    EXPECT_EQ(book.modify(1, 100, 4, trades).status, Status::Accepted);

    add(limit(3, Side::Buy, 100, 4));
    ASSERT_EQ(trades.size(), 1u);
    EXPECT_EQ(trades[0].sell_id, 1u);  // still first in the queue
}

}  // namespace
}  // namespace lob
