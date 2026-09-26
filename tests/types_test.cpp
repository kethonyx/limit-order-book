#include "lob/types.hpp"

#include <gtest/gtest.h>

namespace lob {
namespace {

TEST(Types, OrderDesignatedInit) {
    const Order o{.id = 42, .side = Side::Sell, .type = OrderType::Limit, .price = 10'050, .qty = 100};
    EXPECT_EQ(o.id, 42u);
    EXPECT_EQ(o.side, Side::Sell);
    EXPECT_EQ(o.price, 10'050);
    EXPECT_EQ(o.qty, 100);
}

TEST(Types, TradeEquality) {
    const Trade a{.buy_id = 1, .sell_id = 2, .price = 100, .qty = 5};
    Trade b = a;
    EXPECT_EQ(a, b);
    b.qty = 6;
    EXPECT_NE(a, b);
}

TEST(Types, OppositeSide) {
    static_assert(opposite(Side::Buy) == Side::Sell);
    static_assert(opposite(Side::Sell) == Side::Buy);
}

TEST(Types, ToString) {
    EXPECT_EQ(to_string(Side::Buy), "BUY");
    EXPECT_EQ(to_string(OrderType::Market), "MARKET");
    EXPECT_EQ(to_string(Status::UnknownId), "UNKNOWN_ID");
}

}  // namespace
}  // namespace lob
