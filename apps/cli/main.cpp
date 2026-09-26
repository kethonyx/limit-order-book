// Order book CLI demo.
//
//   lob_cli [file]      read commands from a file, or from stdin if omitted
//
// See include/lob/parser.hpp for the command grammar, examples/orders.txt
// for a sample session.

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "lob/order_book.hpp"
#include "lob/parser.hpp"

namespace {

using namespace lob;

constexpr std::size_t kPrintDepth = 5;

void print_trades(const std::vector<Trade>& trades) {
    for (const Trade& t : trades) {
        std::cout << "TRADE buy=" << t.buy_id << " sell=" << t.sell_id << " price=" << format_price(t.price)
                  << " qty=" << t.qty << '\n';
    }
}

void print_book(const OrderBook& book) {
    const auto bids = book.depth(Side::Buy, kPrintDepth);
    const auto asks = book.depth(Side::Sell, kPrintDepth);

    std::cout << "---------------- BOOK ----------------\n";
    std::cout << std::right << std::setw(18) << "BID qty @ price" << " | " << "ASK price x qty\n";
    const std::size_t rows = std::max(bids.size(), asks.size());
    for (std::size_t i = 0; i < rows; ++i) {
        std::string left;
        std::string right;
        if (i < bids.size()) left = std::to_string(bids[i].qty) + " @ " + format_price(bids[i].price);
        if (i < asks.size()) right = format_price(asks[i].price) + " x " + std::to_string(asks[i].qty);
        std::cout << std::right << std::setw(18) << left << " | " << right << '\n';
    }
    if (rows == 0) std::cout << "             (empty)\n";

    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    std::cout << "TOP bid=" << (bid ? format_price(*bid) : "-") << " ask=" << (ask ? format_price(*ask) : "-")
              << '\n';
    std::cout << "--------------------------------------\n";
    std::cout.flush();
}

void execute(OrderBook& book, const Command& cmd, std::vector<Trade>& trades) {
    trades.clear();
    switch (cmd.kind) {
        case CommandKind::Empty: return;
        case CommandKind::Print: print_book(book); return;
        case CommandKind::Cancel: {
            const Status s = book.cancel(cmd.order.id);
            std::cout << (s == Status::Accepted ? "CANCELLED" : "REJECT") << " id=" << cmd.order.id;
            if (s != Status::Accepted) std::cout << ' ' << to_string(s);
            std::cout << '\n';
            return;
        }
        case CommandKind::Modify:
        case CommandKind::Add: {
            const ExecResult r = cmd.kind == CommandKind::Add
                                     ? book.add(cmd.order, trades)
                                     : book.modify(cmd.order.id, cmd.order.price, cmd.order.qty, trades);
            if (r.status != Status::Accepted) {
                std::cout << "REJECT id=" << cmd.order.id << ' ' << to_string(r.status) << '\n';
                return;
            }
            print_trades(trades);
            if (r.cancelled > 0) {
                std::cout << "IOC_CANCEL id=" << cmd.order.id << " unfilled=" << r.cancelled
                          << " (insufficient liquidity)\n";
            }
            return;
        }
    }
}

int run(std::istream& in) {
    OrderBook book;
    std::vector<Trade> trades;
    std::string line;
    int line_no = 0;
    int errors = 0;

    while (std::getline(in, line)) {
        ++line_no;
        const ParseResult parsed = parse_line(line);
        if (!parsed.ok()) {
            std::cerr << "error: line " << line_no << ": " << parsed.error << '\n';
            ++errors;
            continue;
        }
        execute(book, parsed.command, trades);
    }

    print_book(book);
    return errors == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    if (argc > 2) {
        std::cerr << "usage: " << argv[0] << " [orders-file]\n";
        return 2;
    }
    if (argc == 2) {
        std::ifstream file(argv[1]);
        if (!file) {
            std::cerr << "error: cannot open " << argv[1] << '\n';
            return 2;
        }
        return run(file);
    }
    return run(std::cin);
}
