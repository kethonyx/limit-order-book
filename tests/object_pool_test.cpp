#include "lob/object_pool.hpp"

#include <gtest/gtest.h>

#include <set>
#include <utility>
#include <vector>

#include "lob/order_book.hpp"

namespace lob {
namespace {

struct Point {
    int x;
    int y;
};

TEST(ObjectPool, AcquireConstructsAndReleaseRecycles) {
    ObjectPool<Point> pool(4);
    Point* a = pool.acquire(Point{1, 2});
    EXPECT_EQ(a->x, 1);
    EXPECT_EQ(a->y, 2);
    EXPECT_EQ(pool.in_use(), 1u);

    pool.release(a);
    EXPECT_EQ(pool.in_use(), 0u);
    EXPECT_EQ(pool.acquire(Point{3, 4}), a);  // LIFO reuse of the freed slot
}

TEST(ObjectPool, GrowsByChunksAndAddressesAreUnique) {
    ObjectPool<Point> pool(4);
    std::set<Point*> seen;
    for (int i = 0; i < 10; ++i) seen.insert(pool.acquire(Point{i, i}));
    EXPECT_EQ(seen.size(), 10u);
    EXPECT_EQ(pool.capacity(), 12u);  // 3 chunks of 4
}

TEST(ObjectPool, MoveLeavesSourceEmptyButUsable) {
    ObjectPool<Point> a(2);
    Point* p = a.acquire(Point{7, 7});
    ObjectPool<Point> b = std::move(a);
    EXPECT_EQ(p->x, 7);  // address stable across move
    EXPECT_EQ(b.in_use(), 1u);
    EXPECT_EQ(a.in_use(), 0u);  // NOLINT(bugprone-use-after-move): testing moved-from state
    Point* q = a.acquire(Point{1, 1});
    EXPECT_EQ(q->x, 1);
}

TEST(OrderBookMove, MovedBookKeepsWorking) {
    OrderBook a;
    std::vector<Trade> trades;
    a.add({.id = 1, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .qty = 5}, trades);
    OrderBook b = std::move(a);

    EXPECT_EQ(b.cancel(1), Status::Accepted);
    b.add({.id = 2, .side = Side::Sell, .type = OrderType::Limit, .price = 100, .qty = 5}, trades);
    b.add({.id = 3, .side = Side::Buy, .type = OrderType::Limit, .price = 100, .qty = 5}, trades);
    EXPECT_EQ(trades.size(), 1u);
}

}  // namespace
}  // namespace lob
