# Low-Latency C++ Limit Order Book

[![CI](https://github.com/duskWudi/low_latency_orderbook/actions/workflows/ci.yml/badge.svg)](https://github.com/duskWudi/low_latency_orderbook/actions/workflows/ci.yml)

A limit order book and matching engine in C++17. It handles price-time
priority matching, cancellation, trade generation, replaying events from a
text file, and reporting p50/p99 latency per operation.

I wrote it to see how fast a book gets once you remove the two things that
usually dominate the hot path: heap allocation per operation, and pointer
chasing through node-based containers. On my laptop a synthetic 1M-operation
workload runs at roughly 7.9M ops/s with a median ADD around 100 ns.

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

## Benchmark

`tools/benchmark.cpp` generates a deterministic mix of adds and cancels
(about 15% cancels) clustered in a 21-tick band around price 10000, so the
book genuinely crosses and produces trades instead of just piling up resting
orders. It replays that workload twice: once clean for throughput, once with
timing around every operation for latency.

From one run of `./orderbook_bench 1000000` at `-O2`:

| Metric         | Value                    |
| -------------- | ------------------------ |
| Operations     | 1,000,000                |
| Throughput     | ~7.9 M ops/s             |
| ADD latency    | p50 ~100 ns, p99 ~600 ns |
| CANCEL latency | p50 ~100 ns, p99 ~700 ns |

Treat these as a rough shape, not a spec. They move around by machine and
by build, and the `max` figure is always an outlier because the first
operation pays for page commits and a cold cache. Note also that the timing
pass calls `high_resolution_clock` twice per operation, which is not free
when the operation itself is ~100 ns.

## Not done yet

Single instrument, single thread. Only limit orders, so no market, IOC, or
FOK, and no order modification. The order id lookup is still
`std::unordered_map`, which is the first thing I would replace with an
open-addressing map. Parsing is one line at a time, so large replay files
would benefit from batching. A slab allocator sized to cache lines would be
worth trying too.
