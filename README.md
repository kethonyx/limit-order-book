# limit-order-book

A single-instrument **limit order book matching engine** in C++20. Supports limit, market, cancel and
modify orders with price-time priority, and was measured before and after one allocation-focused
optimization. Every benchmark number below comes from runs recorded in [`bench/results/`](bench/results/).

```
LIMIT 6 BUY 101.50 90          # sweeps two ask levels
TRADE buy=6 sell=1 price=101.00 qty=50
TRADE buy=6 sell=2 price=101.00 qty=30
TRADE buy=6 sell=3 price=101.50 qty=10
```

## Features

- **Order types:** limit, market (immediate-or-cancel), cancel, modify.
- **Price-time priority:** best price first, FIFO within a price level.
- **Partial fills,** including one incoming order sweeping several price levels.
- **Trade events** `(buy_id, sell_id, price, qty)`, always at the **resting** order's price.
- **Integer tick prices.** No floating point anywhere, including input parsing (`"101.25"` → `10125`).
- **Validation with status codes instead of exceptions:** `INVALID_QTY`, `INVALID_PRICE`,
  `DUPLICATE_ID`, `UNKNOWN_ID`. A rejected order has no side effects.
- **Tests:** 58 unit tests, plus a randomized differential test against a naive reference model. All
  of them run under AddressSanitizer and UndefinedBehaviorSanitizer.

## Behaviour decisions

| Situation | Behaviour | Why |
|---|---|---|
| Trade price | Resting order's price | The resting order set the price; the aggressor accepted it and may get price improvement |
| Market order, not enough liquidity | Fill what exists, **cancel the remainder**, report it in `ExecResult::cancelled` | A market order has no price, so resting it would be meaningless. This is IOC semantics, as most venues use |
| Market order on an empty book | Accepted, `filled = 0`, `cancelled = qty` | Same rule; not an input error |
| Limit remainder after sweeping | Rests at **its own limit price**, not at the last fill price | Standard behaviour; the order's price is what its owner agreed to |
| `qty <= 0`, limit `price <= 0` | Rejected | Equity-style market. Types are signed so bad input like `-5` is detectable rather than wrapping around |
| Cancel of an unknown, filled or already-cancelled id | `UNKNOWN_ID` | Filled orders leave the book; the engine does not keep history |
| Order id uniqueness | Enforced among **live** orders only | Remembering every id forever would mean unbounded memory. Real venues scope ids per session |
| Modify: same price, smaller qty | In place, **keeps time priority** | Doesn't hurt anyone queued behind it |
| Modify: new price or larger qty | Cancel and re-add, **loses priority**, may trade immediately | Otherwise a trader could jump the queue |
| Self-trade | Not prevented | Out of scope; see Limitations |

### Why integer ticks and not `double`

`0.1` has no exact binary representation, so `0.1 + 0.2 != 0.3`. With `double` prices, two orders at
the "same" price could land at different `std::map` keys, and equality checks during matching would be
unreliable. Integer ticks compare exactly and quickly. Converting to and from decimal happens only at
the edges: [`parse_price`](src/parser.cpp) parses digits directly, with no `stod`, and rejects anything
finer than one tick (`1.005` is an error).

## Design

```
            bids_ : std::map<Price, Level, std::greater<>>   best = begin()
            asks_ : std::map<Price, Level, std::less<>>      best = begin()

   Level { total_qty, count, head ─► OrderNode ⇄ OrderNode ⇄ OrderNode ◄─ tail }
                                        ▲  (intrusive, FIFO: head fills first)
   index_: std::unordered_map<OrderId, OrderNode*>
   pool_ : ObjectPool<OrderNode>   (owns every node; chunked slab + free list)
```

- **Price levels in `std::map`.** Levels come out sorted, the best price is `begin()` in O(1), and
  inserting or erasing a level is O(log L), where **L is the number of price levels**, not the number
  of orders. L is small in practice (tens to hundreds), because activity clusters around the spread.
  - A sorted `std::vector` of levels would be more cache-friendly. Most inserts land near the top, so
    storing the vector best-last makes those inserts cheap.
  - An array indexed by `price - base` would give O(1) everywhere, but needs a bounded price range.
  - Both are listed under Further work.
- **Orders within a level form an intrusive doubly-linked list.** Each `OrderNode` stores its own
  `prev`, `next` and `level` pointers. Removal from anywhere in the queue is O(1) with no search, and
  the node already knows its level, so cancel needs no map lookup to find it. The baseline used
  `std::list<Order>`; the optimization replaced it (see Optimization).
- **Id index: `unordered_map<OrderId, OrderNode*>`.** It turns cancel and modify into an O(1)-average
  lookup.
- **Pointer stability is the invariant that makes this work.** `std::map` nodes never move, so a
  `Level*` stays valid until that level is erased, and a level is only erased once it's empty, when
  no node points to it. Pool slots never move either. For the same reason `OrderBook` is
  **non-copyable**: a copy's nodes would point into the original book's levels.
- **Trades are appended to a caller-owned `std::vector<Trade>&`.** The caller reuses one buffer, so
  the hot path doesn't allocate a vector per call. A callback or listener interface is the other
  common choice.

### Complexity

L = price levels on one side, F = resting orders filled by an incoming order, K = levels it empties.

| Operation | Complexity | Notes |
|---|---|---|
| Add limit, no match | O(log L) + O(1) avg | Map lookup or insert of the level, pool acquire, hash insert |
| Add, matching | O(F + K) + O(log L) if a remainder rests | Each fill is O(1). Erasing the emptied `begin()` level is amortized O(1) |
| Market order | O(F + K) | Never rests |
| Cancel | O(1) avg; O(log L) if the level becomes empty | Hash lookup + unlink. An empty level is erased by key |
| Modify, reduce in place | O(1) avg | |
| Modify, otherwise | Cancel + add | |
| Best bid / ask | O(1) | `begin()` of each map |
| `volume_at(price)` | O(log L) | |
| `depth(k)` | O(k) | |

## Tests

```
tests/types_test.cpp         core types
tests/order_book_test.cpp    basic matching, market, cancel, modify
tests/edge_cases_test.cpp    crossing the spread, sweeps, FIFO, cancel edge cases, invalid input, modify
tests/parser_test.cpp        exact decimal parsing, CLI grammar
tests/object_pool_test.cpp   pool reuse and growth, move semantics, moving an OrderBook
tests/randomized_test.cpp    differential test: 5 seeds x 5,000 random ops vs a naive reference book
```

The randomized test drives the engine and a deliberately simple reference model: a flat vector with
linear scans, easy to check by eye. It uses random limit and market orders, cancels of live, filled and
unknown ids, duplicate ids, and invalid quantities in a narrow price band, so orders cross often. After
**every** operation it asserts identical trades, results, best bid/ask and order count, and that the
book is never left crossed. This test is how the optimization rewrite was shown to preserve behaviour.

## Benchmarks

### Method ([`bench/`](bench/))

- **Order flow.** 1,000,000 synthetic operations, seed 42, generated **before** timing starts: 54.9%
  limit, 10.1% market, 35.1% cancel.
  - Limit prices sit −10 to +40 ticks from a fixed mid on the passive side, so most rest and some cross.
  - Cancels target earlier orders, some of which have already filled. That is realistic flow.
  - About 112k orders are resting at the end.
- **Throughput.** One warm-up replay, then 5 timed replays on fresh books, with no per-operation
  timing. The reported figure is the median.
- **Latency.** A separate replay timing each operation with `steady_clock`, giving
  p50/p90/p99/p99.9/max overall and per operation type.
- **Allocations.** A counting global `operator new` in the benchmark binary reports heap allocations
  per operation.
- **Build.** `release` preset: `-O3 -DNDEBUG`, Apple clang 17, no LTO, no `-march` tuning.

**Timer caveat.** On Apple Silicon, `steady_clock` ticks at 24 MHz, so every latency is a multiple of
**~41.7 ns**. The harness measures and prints this. A p50 of "42 ns" means one timer tick. The latency
figures include the cost of reading the clock twice. Treat them as upper bounds with ±1-tick
granularity. Throughput is the more precise number.

### Hardware

MacBook Pro 13" (MacBookPro17,1), **Apple M1** (4 performance + 4 efficiency cores), 8 GB RAM,
macOS 26.3.1, Apple clang 17.0.0.

Conditions: **on battery, with desktop apps running (load average ~4–5)**. macOS doesn't allow
pinning a thread to a core, so the scheduler may move the benchmark between P and E cores.

### Results

Optimization comparison ([`bench/results/optimization_comparison.txt`](bench/results/optimization_comparison.txt)): the
three binaries ran **interleaved**, in 5 rounds, so that machine noise affects all variants equally.
Throughput is the median of the 5 per-invocation medians. Latency ranges span the 5 invocations.

| Variant | Throughput (ops/s) | Mean / op | Heap allocs / op | p50 | p99 | p99.9 | max |
|---|---|---|---|---|---|---|---|
| Baseline: `std::list` per level | 10.97 M | ~91 ns | 1.099 | 83 ns | 250–292 ns | 417–500 ns | 2.2–4.2 ms |
| **Pool + intrusive list** | **14.52 M (+32%)** | ~69 ns | **0.611** | **42 ns** | 209–250 ns | 292–375 ns | 0.97–1.24 ms |
| Pool + index capacity hint | 17.75 M (+62%) | ~56 ns | 0.611 | 42 ns | 208–250 ns | 292–375 ns | **19–37 µs** |

Per-round throughput (ops/s):
baseline 10.70M–11.13M · pool 14.08M–14.58M · pool+reserve 17.06M–17.84M. The ranges don't overlap.

Why the absolute numbers need care: in the earlier baseline run
([`baseline.txt`](bench/results/baseline.txt)), made at a different time under
different background load, the same baseline binary gave medians of only 6.3M–6.4M ops/s.
**Compare variants only within the interleaved run.** Rerun on your own machine,
plugged in and idle, for absolute figures.

## Optimization

**Problem.** In the baseline, each resting order caused about two heap allocations: a `std::list` node
and an `unordered_map` node. That is 1.099 allocations per operation. `malloc` on the hot path costs
time, adds latency jitter, and scatters orders across the heap.

**Change.**
1. **`ObjectPool<OrderNode>`** ([`object_pool.hpp`](include/lob/object_pool.hpp)) allocates 4096-slot
   chunks and threads free slots onto an intrusive free list, so `acquire` and `release` are O(1)
   pointer swaps. Slots are reused LIFO, so a newly rested order usually lands in cache-warm memory.
   It requires trivially destructible `T`, because the pool doesn't track live slots. Moving a pool
   empties the source; a defaulted move would have left a dangling free-list pointer.
2. **An intrusive FIFO replaces `std::list<Order>`.** Nodes carry their own `prev`, `next` and
   `level` pointers.

**Result.** 1.099 → 0.611 allocations per operation, +32% throughput, and p50 down from two timer
ticks to one.

**The remaining ~1 ms outliers.** They are `unordered_map` **rehashes**: at around 100k entries, a
rehash moves every node at once. The optional `OrderBook(expected_orders)` constructor pre-sizes the
index, which removes the rehashes. The max dropped from ~1 ms to 19–37 µs, and throughput rose a
further 22%. This is opt-in because it trades memory for predictability. The capacity has to be known
up front, which is usually true for an exchange's daily limits.

**Still allocating (0.611/op).** One `unordered_map` node per resting order, plus a `std::map` node per
new price level. The next step would be an open-addressing hash map for the index.

## Build and run

Requirements: CMake ≥ 3.20, Ninja, and a C++20 compiler (tested with Apple clang 17). GoogleTest
v1.18.0 is fetched automatically.

```bash
# Debug + ASan/UBSan, run all tests
cmake --preset debug && cmake --build --preset debug && ctest --preset debug

# Release (optimised) build: benchmark + CLI
cmake --preset release && cmake --build --preset release
./build/release/bench/lob_bench                    # 1M ops, 5 runs
./build/release/bench/lob_bench 1000000 5 262144   # with index capacity hint

# CLI demo: from a file or stdin
./build/release/lob_cli examples/orders.txt
echo "LIMIT 1 SELL 10.00 5
MARKET 2 BUY 8" | ./build/release/lob_cli
```

### CLI grammar

```
LIMIT  <id> BUY|SELL <price> <qty>     price in dollars, tick 0.01
MARKET <id> BUY|SELL <qty>
CANCEL <id>
MODIFY <id> <price> <qty>
PRINT                                  prints the top 5 levels per side
# comment
```

Output lines are `TRADE`, `REJECT <reason>`, `CANCELLED`, and `IOC_CANCEL` (the unfilled remainder of
a market order). Malformed lines go to stderr with their line number and are skipped; the process then
exits with status 1.

## Project layout

```
include/lob/   types.hpp, order_book.hpp, object_pool.hpp, parser.hpp
src/           order_book.cpp, parser.cpp, types.cpp
apps/cli/      lob_cli demo
tests/         GoogleTest suites (see Tests)
bench/         order flow generator, harness, results/
cmake/         warning and sanitizer helpers
```

## Limitations and further work

- **Single-threaded and single-instrument.** Real engines usually run one matching thread per
  instrument, sharded by symbol and fed by lock-free SPSC queues. Matching one book stays sequential,
  because price-time priority is a total order.
- **No self-trade prevention, stop orders, iceberg orders or time-in-force** beyond IOC for market
  orders.
- **Cancel is O(log L) when it empties a level.** Storing the level's map iterator would make it
  amortized O(1), at the cost of per-side iterator types.
- **Pool memory is never returned to the OS** until the book is destroyed.
- **Next optimizations to measure:**
  - An open-addressing id index, which would remove the last per-order allocation.
  - A flat or array-based price-level structure.
  - `-march=native` and LTO.
  - Google Benchmark for statistically rigorous repetitions.
