#include "lob/parser.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <vector>

namespace lob {
namespace {

template <class Int>
std::optional<Int> parse_int(std::string_view text) {
    Int value{};
    const char* first = text.data();
    const char* last = first + text.size();
    // from_chars: locale-independent, no exceptions, reports overflow.
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last) return std::nullopt;
    return value;
}

bool iequals(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
    });
}

std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        const std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i > start) tokens.push_back(line.substr(start, i - start));
    }
    return tokens;
}

std::optional<Side> parse_side(std::string_view text) {
    if (iequals(text, "BUY")) return Side::Buy;
    if (iequals(text, "SELL")) return Side::Sell;
    return std::nullopt;
}

ParseResult fail(std::string message) { return {.command = {}, .error = std::move(message)}; }

}  // namespace

std::optional<Price> parse_price(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }

    const auto dot = text.find('.');
    const std::string_view whole = text.substr(0, dot);
    const std::string_view frac = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);

    // Reject "", ".", "1.", "1.005" (finer than one tick), and any non-digits.
    constexpr std::size_t kMaxDecimals = 2;  // log10(kTicksPerUnit)
    if (whole.empty() || frac.size() > kMaxDecimals) return std::nullopt;
    if (dot != std::string_view::npos && frac.empty()) return std::nullopt;
    if (!std::ranges::all_of(frac, [](char c) { return c >= '0' && c <= '9'; })) return std::nullopt;

    const auto units = parse_int<Price>(whole);
    if (!units || *units < 0) return std::nullopt;  // sign was already consumed
    if (*units > std::numeric_limits<Price>::max() / kTicksPerUnit) return std::nullopt;

    Price ticks = *units * kTicksPerUnit;
    Price scale = kTicksPerUnit;
    for (const char c : frac) {
        scale /= 10;
        ticks += (c - '0') * scale;
    }
    return negative ? -ticks : ticks;
}

std::string format_price(Price ticks) {
    const bool negative = ticks < 0;
    // Work in unsigned to avoid overflow when negating INT64_MIN.
    const auto magnitude = negative ? 0 - static_cast<std::uint64_t>(ticks) : static_cast<std::uint64_t>(ticks);
    const auto per_unit = static_cast<std::uint64_t>(kTicksPerUnit);
    const std::uint64_t cents = magnitude % per_unit;

    std::string out = negative ? "-" : "";
    out += std::to_string(magnitude / per_unit);
    out += '.';
    if (cents < 10) out += '0';
    out += std::to_string(cents);
    return out;
}

ParseResult parse_line(std::string_view line) {
    if (const auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
    const auto tok = tokenize(line);
    if (tok.empty()) return {};

    const std::string_view verb = tok[0];

    if (iequals(verb, "PRINT")) {
        if (tok.size() != 1) return fail("usage: PRINT");
        return {.command = {.kind = CommandKind::Print, .order = {}}, .error = {}};
    }

    if (iequals(verb, "CANCEL")) {
        if (tok.size() != 2) return fail("usage: CANCEL <id>");
        const auto id = parse_int<OrderId>(tok[1]);
        if (!id) return fail("invalid order id");
        return {.command = {.kind = CommandKind::Cancel, .order = {.id = *id}}, .error = {}};
    }

    if (iequals(verb, "MODIFY")) {
        if (tok.size() != 4) return fail("usage: MODIFY <id> <price> <qty>");
        const auto id = parse_int<OrderId>(tok[1]);
        const auto price = parse_price(tok[2]);
        const auto qty = parse_int<Qty>(tok[3]);
        if (!id) return fail("invalid order id");
        if (!price) return fail("invalid price (max 2 decimals)");
        if (!qty) return fail("invalid quantity");
        Order o{.id = *id, .price = *price, .qty = *qty};
        return {.command = {.kind = CommandKind::Modify, .order = o}, .error = {}};
    }

    const bool is_limit = iequals(verb, "LIMIT");
    const bool is_market = iequals(verb, "MARKET");
    if (!is_limit && !is_market) return fail("unknown command '" + std::string(verb) + "'");
    if (is_limit && tok.size() != 5) return fail("usage: LIMIT <id> BUY|SELL <price> <qty>");
    if (is_market && tok.size() != 4) return fail("usage: MARKET <id> BUY|SELL <qty>");

    const auto id = parse_int<OrderId>(tok[1]);
    const auto side = parse_side(tok[2]);
    if (!id) return fail("invalid order id");
    if (!side) return fail("side must be BUY or SELL");

    Order o{.id = *id, .side = *side, .type = is_limit ? OrderType::Limit : OrderType::Market};
    if (is_limit) {
        const auto price = parse_price(tok[3]);
        if (!price) return fail("invalid price (max 2 decimals)");
        o.price = *price;
    }
    // Negative / zero quantities parse fine on purpose: the engine rejects
    // them with INVALID_QTY, which the demo can show.
    const auto qty = parse_int<Qty>(tok[is_limit ? 4 : 3]);
    if (!qty) return fail("invalid quantity");
    o.qty = *qty;

    return {.command = {.kind = CommandKind::Add, .order = o}, .error = {}};
}

}  // namespace lob
