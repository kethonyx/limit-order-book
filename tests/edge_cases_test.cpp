// Edge cases for matching, cancel and validation.

#include <gtest/gtest.h>

#include <vector>

#include "lob/order_book.hpp"

namespace lob {
namespace {

Order limit(OrderId id, Side side, Price price, Qty qty) {
    return {.id = id, .side = side, .type = OrderType::Limit, .price = price, .qty = qty};
}

Order market(OrderId id, Side side, Qty qty) {
    return {.id = id, .side = side, .type = OrderType::Market, .price = 0, .qty = qty};
}

Trade trade(OrderId buy, OrderId sell, Price price, Qty qty) {
    return {.buy_id = buy, .sell_id = sell, .price = price, .qty = qty};
}

class EdgeCases : public ::testing::Test {
protected:
    ExecResult add(const Order& o) { return book.add(o, trades); }

    OrderBook book;
    std::vector<Trade> trades;
};

// --- Crossing the spread ------------------------------------------------------

TEST_F(EdgeCases, BuyAboveAskExecutesAtAskPrice) {
    add(limit(1, Side::Sell, 100, 10));
    add(limit(2, Side::Buy, 120, 10));
    EXPECT_EQ(trades, std::vector{trade(2, 1, 100, 10)});  // price improvement for the buyer
}

TEST_F(EdgeCases, SellBelowBidExecutesAtBidPrice) {
    add(limit(1, Side::Buy, 100, 10));
    add(limit(2, Side::Sell, 80, 10));
    EXPECT_EQ(trades, std::vector{trade(1, 2, 100, 10)});
}

TEST_F(EdgeCases, PriceEqualToOppositeBestCrosses) {
    add(limit(1, Side::Sell, 100, 10));
    add(limit(2, Side::Buy, 100, 10));
    EXPECT_EQ(trades.size(), 1u);
}

TEST_F(EdgeCases, OneTickAwayDoesNotCross) {
    add(limit(1, Side::Sell, 101, 10));
    add(limit(2, Side::Buy, 100, 10));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.best_bid(), 100);
    EXPECT_EQ(book.best_ask(), 101);
}

// --- Partial fills across levels ---------------------------------------------

TEST_F(EdgeCases, SweepsLevelsInPriceOrderAndStopsAtLimit) {
    add(limit(1, Side::Sell, 103, 10));  // inserted out of price order on purpose
    add(limit(2, Side::Sell, 101, 10));
    add(limit(3, Side::Sell, 102, 10));
    add(limit(4, Side::Sell, 104, 10));  // beyond the buyer's limit

    const auto r = add(limit(5, Side::Buy, 103, 35));

    EXPECT_EQ(trades, (std::vector{trade(5, 2, 101, 10), trade(5, 3, 102, 10), trade(5, 1, 103, 10)}));
    EXPECT_EQ(r.filled, 30);
    EXPECT_EQ(r.rested, 5);
    // Remainder rests at the buyer's own limit, not at the last fill price.
    EXPECT_EQ(book.best_bid(), 103);
    EXPECT_EQ(book.volume_at(Side::Buy, 103), 5);
    EXPECT_EQ(book.best_ask(), 104);
    EXPECT_EQ(book.volume_at(Side::Sell, 104), 10);
}

TEST_F(EdgeCases, PartiallyFilledRestingOrderKeepsRemainder) {
    add(limit(1, Side::Sell, 100, 10));
    add(limit(2, Side::Buy, 100, 3));

    EXPECT_EQ(book.volume_at(Side::Sell, 100), 7);
    EXPECT_TRUE(book.contains(1));
    EXPECT_FALSE(book.contains(2));
}

TEST_F(EdgeCases, TradesAreAppendedNotCleared) {
    trades.push_back(trade(0, 0, 0, 0));
    add(limit(1, Side::Sell, 100, 1));
    add(limit(2, Side::Buy, 100, 1));
    EXPECT_EQ(trades.size(), 2u);
}

// --- FIFO at the same price --------------------------------------------------

TEST_F(EdgeCases, FifoWithinLevel) {
    add(limit(1, Side::Buy, 100, 5));
    add(limit(2, Side::Buy, 100, 5));
    add(limit(3, Side::Buy, 100, 5));
    add(limit(4, Side::Sell, 100, 12));

    EXPECT_EQ(trades, (std::vector{trade(1, 4, 100, 5), trade(2, 4, 100, 5), trade(3, 4, 100, 2)}));
}

TEST_F(EdgeCases, PartiallyFilledOrderStaysAtFrontOfQueue) {
    add(limit(1, Side::Sell, 100, 10));
    add(limit(2, Side::Sell, 100, 10));
    add(limit(3, Side::Buy, 100, 4));  // #1 left with 6
    trades.clear();
    add(limit(4, Side::Buy, 100, 6));

    EXPECT_EQ(trades, std::vector{trade(4, 1, 100, 6)});
}

TEST_F(EdgeCases, BetterPriceBeatsEarlierTime) {
    add(limit(1, Side::Sell, 101, 5));
    add(limit(2, Side::Sell, 100, 5));  // later, but better price
    add(limit(3, Side::Buy, 101, 5));

    EXPECT_EQ(trades, std::vector{trade(3, 2, 100, 5)});
}

TEST_F(EdgeCases, CancelInMiddlePreservesOrderOfOthers) {
    add(limit(1, Side::Sell, 100, 5));
    add(limit(2, Side::Sell, 100, 5));
    add(limit(3, Side::Sell, 100, 5));
    ASSERT_EQ(book.cancel(2), Status::Accepted);
    add(limit(4, Side::Buy, 100, 10));

    EXPECT_EQ(trades, (std::vector{trade(4, 1, 100, 5), trade(4, 3, 100, 5)}));
}

// --- Cancel ------------------------------------------------------------------

TEST_F(EdgeCases, CancelNonExistentOrder) {
    EXPECT_EQ(book.cancel(42), Status::UnknownId);
}

TEST_F(EdgeCases, CancelFullyFilledOrder) {
    add(limit(1, Side::Sell, 100, 5));
    add(limit(2, Side::Buy, 100, 5));
    EXPECT_EQ(book.cancel(1), Status::UnknownId);
    EXPECT_EQ(book.cancel(2), Status::UnknownId);  // aggressor never rested
}

TEST_F(EdgeCases, CancelTwice) {
    add(limit(1, Side::Buy, 100, 5));
    EXPECT_EQ(book.cancel(1), Status::Accepted);
    EXPECT_EQ(book.cancel(1), Status::UnknownId);
}

TEST_F(EdgeCases, CancelPartiallyFilledRemovesOnlyRemainder) {
    add(limit(1, Side::Sell, 100, 10));
    add(limit(2, Side::Sell, 100, 10));
    add(limit(3, Side::Buy, 100, 4));
    ASSERT_EQ(book.cancel(1), Status::Accepted);

    EXPECT_EQ(book.volume_at(Side::Sell, 100), 10);
    EXPECT_EQ(book.depth(Side::Sell, 5), (std::vector{LevelView{.price = 100, .qty = 10, .orders = 1}}));
}

TEST_F(EdgeCases, CancelLastOrderInBestLevelUpdatesBest) {
    add(limit(1, Side::Buy, 101, 5));
    add(limit(2, Side::Buy, 100, 5));
    ASSERT_EQ(book.cancel(1), Status::Accepted);
    EXPECT_EQ(book.best_bid(), 100);
}

TEST_F(EdgeCases, IdCanBeReusedAfterOrderLeavesBook) {
    // Uniqueness is enforced among live orders only (see README).
    add(limit(1, Side::Buy, 100, 5));
    book.cancel(1);
    EXPECT_EQ(add(limit(1, Side::Sell, 200, 5)).status, Status::Accepted);
}

// --- Market orders -----------------------------------------------------------

TEST_F(EdgeCases, MarketOrderOnEmptyBook) {
    const auto r = add(market(1, Side::Buy, 10));
    EXPECT_EQ(r, (ExecResult{.status = Status::Accepted, .filled = 0, .rested = 0, .cancelled = 10}));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST_F(EdgeCases, MarketOrderSweepsAllLevels) {
    add(limit(1, Side::Buy, 100, 5));
    add(limit(2, Side::Buy, 90, 5));
    add(limit(3, Side::Buy, 1, 5));
    add(market(4, Side::Sell, 15));

    EXPECT_EQ(trades, (std::vector{trade(1, 4, 100, 5), trade(2, 4, 90, 5), trade(3, 4, 1, 5)}));
    EXPECT_FALSE(book.best_bid());
}

TEST_F(EdgeCases, MarketOrderIgnoresPriceField) {
    add(limit(1, Side::Sell, 500, 5));
    Order m = market(2, Side::Buy, 5);
    m.price = 1;  // would not cross as a limit
    EXPECT_EQ(add(m).filled, 5);
}

// --- Invalid input -----------------------------------------------------------

TEST_F(EdgeCases, ZeroAndNegativeQuantityRejected) {
    EXPECT_EQ(add(limit(1, Side::Buy, 100, 0)).status, Status::InvalidQty);
    EXPECT_EQ(add(limit(2, Side::Buy, 100, -5)).status, Status::InvalidQty);
    EXPECT_EQ(add(market(3, Side::Sell, 0)).status, Status::InvalidQty);
    EXPECT_EQ(add(market(4, Side::Sell, -1)).status, Status::InvalidQty);
}

TEST_F(EdgeCases, NonPositiveLimitPriceRejected) {
    EXPECT_EQ(add(limit(1, Side::Buy, 0, 5)).status, Status::InvalidPrice);
    EXPECT_EQ(add(limit(2, Side::Sell, -100, 5)).status, Status::InvalidPrice);
}

TEST_F(EdgeCases, RejectedOrderHasNoSideEffects) {
    add(limit(1, Side::Sell, 100, 5));
    const auto r = add(limit(2, Side::Buy, 100, -5));

    EXPECT_EQ(r, (ExecResult{.status = Status::InvalidQty}));
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(book.volume_at(Side::Sell, 100), 5);
    EXPECT_FALSE(book.contains(2));
}

TEST_F(EdgeCases, DuplicateIdRejectedEvenOnOtherSide) {
    add(limit(1, Side::Buy, 100, 5));
    EXPECT_EQ(add(limit(1, Side::Sell, 100, 5)).status, Status::DuplicateId);
    EXPECT_TRUE(trades.empty());  // must not trade against itself
}

// --- Modify ------------------------------------------------------------------

TEST_F(EdgeCases, ModifyUnknownOrder) {
    EXPECT_EQ(book.modify(9, 100, 5, trades).status, Status::UnknownId);
}

TEST_F(EdgeCases, ModifyWithInvalidValuesLeavesOrderUntouched) {
    add(limit(1, Side::Buy, 100, 5));
    EXPECT_EQ(book.modify(1, 100, 0, trades).status, Status::InvalidQty);
    EXPECT_EQ(book.modify(1, 0, 5, trades).status, Status::InvalidPrice);
    EXPECT_EQ(book.volume_at(Side::Buy, 100), 5);
}

TEST_F(EdgeCases, ModifyIncreaseLosesPriority) {
    add(limit(1, Side::Sell, 100, 5));
    add(limit(2, Side::Sell, 100, 5));
    book.modify(1, 100, 6, trades);
    add(limit(3, Side::Buy, 100, 5));

    EXPECT_EQ(trades, std::vector{trade(3, 2, 100, 5)});
}

TEST_F(EdgeCases, ModifyRepriceThatCrossesTrades) {
    add(limit(1, Side::Sell, 101, 5));
    add(limit(2, Side::Buy, 100, 5));
    const auto r = book.modify(2, 101, 5, trades);

    EXPECT_EQ(r.filled, 5);
    EXPECT_EQ(trades, std::vector{trade(2, 1, 101, 5)});
    EXPECT_EQ(book.order_count(), 0u);
}

}  // namespace
}  // namespace lob
