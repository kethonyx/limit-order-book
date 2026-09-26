#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "lob/types.hpp"

namespace lob {

// Tick size of the demo instrument: 0.01, i.e. 100 ticks per currency unit.
inline constexpr Price kTicksPerUnit = 100;

// Exact decimal -> ticks conversion, no floating point involved:
// "101.25" -> 10125, "7" -> 700, "-1.5" -> -150.
// nullopt for malformed input, more decimals than the tick allows
// ("1.005"), or overflow.
[[nodiscard]] std::optional<Price> parse_price(std::string_view text);

// Ticks -> "101.25".
[[nodiscard]] std::string format_price(Price ticks);

enum class CommandKind : std::uint8_t { Empty, Add, Cancel, Modify, Print };

// One line of CLI input. Grammar (keywords are case-insensitive, '#' starts a comment):
//   LIMIT  <id> BUY|SELL <price> <qty>
//   MARKET <id> BUY|SELL <qty>
//   CANCEL <id>
//   MODIFY <id> <price> <qty>
//   PRINT
struct Command {
    CommandKind kind{CommandKind::Empty};
    Order order;  // Add: full order; Cancel: id; Modify: id, price, qty
};

struct ParseResult {
    Command command;
    std::string error;  // empty on success

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] ParseResult parse_line(std::string_view line);

}  // namespace lob
