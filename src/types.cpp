#include "lob/types.hpp"

namespace lob {

std::string_view to_string(Side s) noexcept {
    switch (s) {
        case Side::Buy: return "BUY";
        case Side::Sell: return "SELL";
    }
    return "?";
}

std::string_view to_string(OrderType t) noexcept {
    switch (t) {
        case OrderType::Limit: return "LIMIT";
        case OrderType::Market: return "MARKET";
    }
    return "?";
}

std::string_view to_string(Status s) noexcept {
    switch (s) {
        case Status::Accepted: return "ACCEPTED";
        case Status::InvalidQty: return "INVALID_QTY";
        case Status::InvalidPrice: return "INVALID_PRICE";
        case Status::DuplicateId: return "DUPLICATE_ID";
        case Status::UnknownId: return "UNKNOWN_ID";
    }
    return "?";
}

}  // namespace lob
