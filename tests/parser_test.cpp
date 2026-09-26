#include "lob/parser.hpp"

#include <gtest/gtest.h>

namespace lob {
namespace {

TEST(ParsePrice, ExactDecimalConversion) {
    EXPECT_EQ(parse_price("101.25"), 10125);
    EXPECT_EQ(parse_price("7"), 700);
    EXPECT_EQ(parse_price("0.1"), 10);
    EXPECT_EQ(parse_price("0.07"), 7);
    EXPECT_EQ(parse_price("-1.5"), -150);
}

TEST(ParsePrice, RejectsSubTickAndMalformed) {
    EXPECT_FALSE(parse_price("1.005"));  // finer than one tick
    EXPECT_FALSE(parse_price(""));
    EXPECT_FALSE(parse_price("."));
    EXPECT_FALSE(parse_price("1."));
    EXPECT_FALSE(parse_price("abc"));
    EXPECT_FALSE(parse_price("1.2x"));
    EXPECT_FALSE(parse_price("99999999999999999999"));  // overflow
}

TEST(FormatPrice, RoundTrips) {
    EXPECT_EQ(format_price(10125), "101.25");
    EXPECT_EQ(format_price(7), "0.07");
    EXPECT_EQ(format_price(-150), "-1.50");
}

TEST(ParseLine, LimitOrder) {
    const auto r = parse_line("limit 7 buy 100.50 20  # comment");
    ASSERT_TRUE(r.ok()) << r.error;
    EXPECT_EQ(r.command.kind, CommandKind::Add);
    EXPECT_EQ(r.command.order,
              (Order{.id = 7, .side = Side::Buy, .type = OrderType::Limit, .price = 10050, .qty = 20}));
}

TEST(ParseLine, MarketCancelModifyPrint) {
    EXPECT_EQ(parse_line("MARKET 1 SELL 5").command.order.type, OrderType::Market);
    EXPECT_EQ(parse_line("CANCEL 42").command.kind, CommandKind::Cancel);
    EXPECT_EQ(parse_line("MODIFY 42 1.00 3").command.kind, CommandKind::Modify);
    EXPECT_EQ(parse_line("PRINT").command.kind, CommandKind::Print);
    EXPECT_EQ(parse_line("   # only a comment").command.kind, CommandKind::Empty);
}

TEST(ParseLine, NegativeQtyParsesSoEngineCanRejectIt) {
    const auto r = parse_line("LIMIT 1 BUY 1.00 -5");
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.command.order.qty, -5);
}

TEST(ParseLine, Errors) {
    EXPECT_FALSE(parse_line("FOO 1").ok());
    EXPECT_FALSE(parse_line("LIMIT 1 HOLD 1.00 5").ok());
    EXPECT_FALSE(parse_line("LIMIT 1 BUY 1.00").ok());
    EXPECT_FALSE(parse_line("LIMIT -1 BUY 1.00 5").ok());  // ids are unsigned
    EXPECT_FALSE(parse_line("CANCEL").ok());
}

}  // namespace
}  // namespace lob
