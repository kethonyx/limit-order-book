#pragma once

#include <cstdint>
#include <string_view>

namespace lob {

// Prices are integer ticks (e.g. 1 tick = $0.01), never floating point:
// binary floats cannot represent most decimal prices exactly, so equal prices
// could compare unequal and land in different price levels. Conversion to and
// from human-readable prices happens only at the edges (parsing / printing).
// Signed so that bad input ("-100") and price differences are representable;
// this engine models an equity-style market and rejects limit prices <= 0.
using Price = std::int64_t;

// Signed on purpose: input like "-5" must be detectable and rejected.
// With an unsigned type it would silently wrap to ~1.8e19.
using Qty = std::int64_t;

using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t { Limit, Market };

// No timestamp: time priority is the order's position in its level's FIFO
// queue, so a timestamp would be redundant state.
struct Order {
    OrderId id{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{};  // ignored for market orders
    Qty qty{};      // remaining quantity

    friend bool operator==(const Order&, const Order&) = default;
};

// Always executes at the resting (passive) order's price.
struct Trade {
    OrderId buy_id{};
    OrderId sell_id{};
    Price price{};
    Qty qty{};

    friend bool operator==(const Trade&, const Trade&) = default;
};

// Rejections are expected outcomes on the hot path, so they are reported as
// values rather than thrown as exceptions.
enum class Status : std::uint8_t {
    Accepted,
    InvalidQty,    // qty <= 0
    InvalidPrice,  // limit price <= 0
    DuplicateId,   // id already resting in the book
    UnknownId,     // cancel of an id not in the book (never existed or fully filled)
};

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] std::string_view to_string(Side s) noexcept;
[[nodiscard]] std::string_view to_string(OrderType t) noexcept;
[[nodiscard]] std::string_view to_string(Status s) noexcept;

}  // namespace lob
