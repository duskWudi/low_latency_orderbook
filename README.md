# Low-Latency C++ Limit Order Book

[![CI](https://github.com/duskWudi/low_latency_orderbook/actions/workflows/ci.yml/badge.svg)](https://github.com/duskWudi/low_latency_orderbook/actions/workflows/ci.yml)

A limit order book and matching engine in C++17. It handles price-time
priority matching, cancellation, trade generation, replaying events from a
text file, and reporting p50/p99 latency per operation.

I wrote it to see how fast a book gets once you remove the two things that
usually dominate the hot path: heap allocation per operation, and pointer
chasing through node-based containers. On my machine a synthetic 1M-operation
workload runs somewhere between 6 and 8M ops/s, with a median add at the
resolution floor of the Windows timer. See
[how the benchmark works](#how-the-benchmark-works) for what that measures
and what it does not.

## How it works

Three choices do most of the work.

Resting orders all live in one `std::vector<OrderNode>` and get recycled
through a free list. Adding an order takes a node off the free list,
cancelling puts it back. Once the pool has grown, add and cancel never call
the allocator.

Price levels are a flat array indexed by price, so the level for a price is
just `levels_[price]`. There is no tree to search. Two cursors, `best_bid_`
and `best_ask_`, track the top of book and only move when a level empties.

Orders at the same price are chained in an intrusive doubly linked list of
pool indices. That keeps FIFO order within a price level and makes
cancellation O(1) once you know which node to unlink, which an
`unordered_map<int, int>` from order id to pool index gives you.

Prices are plain integers, so `10000` can mean $100.00 depending on how you
read it. Nothing here uses floating point, so there is no rounding to argue
about.

The flat price array costs memory: the book reserves a level for every price
from 0 up to `max_price`, whether anyone quotes there or not. That is a good
trade for a single instrument with a known tick range, and a bad one for
anything sparse or unbounded.

The rest is small. `OrderBook` is the engine. `EventParser` turns text lines
into typed events with `string_view` and `from_chars`, so it avoids streams
and per-line allocation. `LatencyStats` collects timings and prints the
percentiles. `Order`, `Trade`, and `Side` are the value types.

## Layout

```
src/        engine, parser, latency stats, demo entry point (main.cpp)
tests/      unit tests (test_orderbook.cpp)
tools/      synthetic benchmark (benchmark.cpp)
data/       sample event file
```

## Building

With CMake:

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

That gives you three binaries: `orderbook`, `orderbook_tests`, and
`orderbook_bench`.

Or straight from g++ if you would rather skip CMake:

```bash
# engine
g++ -std=c++17 -O2 -o orderbook src/main.cpp src/OrderBook.cpp src/EventParser.cpp src/LatencyStats.cpp

# tests
g++ -std=c++17 -O2 -Isrc -o orderbook_tests tests/test_orderbook.cpp src/OrderBook.cpp

# benchmark
g++ -std=c++17 -O2 -Isrc -o orderbook_bench tools/benchmark.cpp src/OrderBook.cpp src/LatencyStats.cpp
```

## Running

With no arguments `orderbook` replays a small built-in demo. Pass a file to
replay that instead:

```bash
./orderbook
./orderbook data/sample_orders.txt
```

Tests and benchmark:

```bash
./orderbook_tests
./orderbook_bench 1000000     # operation count is optional, defaults to 1M
```

## Input format

```txt
ADD <id> <BUY/SELL> <price> <quantity>
CANCEL <id>
PRINT
```

Commands are case-insensitive, `B` and `S` work as shorthand for the side,
blank lines are skipped, and anything starting with `#` is a comment.

```txt
ADD 1 BUY 10000 10
ADD 2 BUY 10100 5
ADD 3 SELL 9900 7
CANCEL 1
PRINT
```

## Not done yet

Single instrument, single thread. Only limit orders, so no market, IOC, or
FOK, and no order modification. The order id lookup is still
`std::unordered_map`, which is the first thing I would replace with an
open-addressing map. Parsing is one line at a time, so large replay files
would benefit from batching. A slab allocator sized to cache lines would be
worth trying too.

## How the benchmark works

`tools/benchmark.cpp` builds the whole workload in memory first, then
replays it. Generation uses a xorshift64 PRNG with a hardcoded seed, so the
same operation count always produces the same sequence and two runs are
comparable.

The mix is about 15% cancels and the rest adds, with random sides,
quantities of 1 to 10, and prices spread over a 21-tick band around 10000.
The narrow band is deliberate: it keeps bids and asks colliding so the
matching path actually runs, instead of building two walls of resting orders
that never trade. Cancel targets are drawn from every id issued so far,
including orders that already filled, so a share of cancels miss and return
`false`. That is intentional, since a real feed cancels late too.

The replay happens twice on two fresh books. The first pass runs clean and
takes one wall-clock reading around the entire loop, which gives throughput.
The second pass brackets every single operation with
`high_resolution_clock`, which gives the p50/p99 split for adds and cancels
separately. They are separate passes because two clock reads per operation
cost real time when the operation itself is around 100 ns, so folding them
into the throughput number would understate it.

```bash
./orderbook_bench 1000000
```

Repeat runs on my machine, g++ 14.2 at `-O2`, Windows 11 x64:

| Metric          | Value                        |
| --------------- | ---------------------------- |
| Operations      | 1,000,000                    |
| Trades          | 615,369                      |
| Resting at end  | 144,887                      |
| Throughput      | 6.3 to 7.6 M ops/s           |
| ADD latency     | p50 100 ns, p99 500 to 600 ns |
| CANCEL latency  | p50 100 ns, p99 600 to 700 ns |

The trade and resting counts are identical run to run, which is the seeded
workload doing its job. Throughput is the noisy part, swinging about 20%
between runs on an unpinned thread with other things on the machine.

Two caveats on the latency figures. Every sample comes back as a multiple of
100 ns with a minimum of 0, because that is the tick resolution of
`high_resolution_clock` on Windows, so a p50 of 100 ns means one tick and the
true median is somewhere at or below it. And `max` lands in the hundreds of
microseconds to low milliseconds, which is not the engine but page commits as
the order pool grows plus the scheduler taking the thread away.

Worth being clear about what this does not cover. The workload is
pre-generated, so `EventParser` and file IO are outside the measured region
and these numbers say nothing about parsing speed. It is single threaded with
no contention, and everything stays hot in cache, which is friendlier than a
real feed would be.
