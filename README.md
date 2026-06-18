# Low-Latency C++ Limit Order Book

[![CI](https://github.com/duskWudi/low_latency_orderbook/actions/workflows/ci.yml/badge.svg)](https://github.com/duskWudi/low_latency_orderbook/actions/workflows/ci.yml)

A simplified C++17 limit order book and matching engine focused on
low-latency design: price-time priority matching, cancellation, trade
generation, file-based event replay, and nanosecond-level latency
measurement.

On a synthetic 1M-operation workload it sustains **~7.9M ops/s** with an
**ADD p50 of ~100 ns** (see [Benchmark](#benchmark)).

## Why this project matters

This is a trading-systems project, not a fake trading bot. It demonstrates:

- C++ object-oriented design
- Trading market structure
- Price-time priority matching
- Data structure choices for low latency
- Latency measurement (p50 / p99)
- Clean command parsing
- Practical systems programming

## Design

The matching engine avoids per-operation heap allocation and the pointer
chasing of node-based containers:

1. **Order memory pool** – all resting orders live in one contiguous
   `std::vector<OrderNode>`, recycled through a free list. After warmup,
   add/cancel never call the global allocator.
2. **Flat array price levels** – `levels_` is indexed directly by price for
   O(1) access to any level. `best_bid_` / `best_ask_` cursors track the top
   of book so matching never searches a tree.
3. **Intrusive linked-list levels** – orders at the same price are chained by
   `prev`/`next` indices into the pool, preserving FIFO price-time priority
   and giving O(1) cancellation.

Supporting pieces:

- `OrderBook` owns the matching logic.
- `EventParser` converts text commands into typed events using
  `std::string_view` + `std::from_chars` (no streams, no per-line allocations).
- `LatencyStats` records and reports p50/p99 operation latency.
- `Order`, `Trade`, and `Side` model the core trading objects.

Prices are integers (e.g. `10000` can mean `$100.00`) to avoid
floating-point precision problems.

## Project layout

```
src/        engine, parser, latency stats, demo entry point (main.cpp)
tests/      unit tests (test_orderbook.cpp)
tools/      synthetic benchmark (benchmark.cpp)
data/       sample event file
```

## Build

### With CMake

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

This produces three targets: `orderbook`, `orderbook_tests`, and
`orderbook_bench`.

### With g++ directly

```bash
# engine
g++ -std=c++17 -O2 -o orderbook src/main.cpp src/OrderBook.cpp src/EventParser.cpp src/LatencyStats.cpp

# tests
g++ -std=c++17 -O2 -Isrc -o orderbook_tests tests/test_orderbook.cpp src/OrderBook.cpp

# benchmark
g++ -std=c++17 -O2 -Isrc -o orderbook_bench tools/benchmark.cpp src/OrderBook.cpp src/LatencyStats.cpp
```

## Run

Built-in demo:

```bash
./orderbook
```

With sample data:

```bash
./orderbook data/sample_orders.txt
```

Tests:

```bash
./orderbook_tests
```

Benchmark (default 1,000,000 operations, optional count argument):

```bash
./orderbook_bench 1000000
```

## Input format

```txt
ADD <id> <BUY/SELL> <price> <quantity>
CANCEL <id>
PRINT
```

Lines beginning with `#` are ignored. Example:

```txt
ADD 1 BUY 10000 10
ADD 2 BUY 10100 5
ADD 3 SELL 9900 7
CANCEL 1
PRINT
```

## Benchmark

A deterministic ADD/CANCEL workload centered on one price band (so the book
actually crosses and trades), replayed once for throughput and once for
per-operation latency.

Example run (`./orderbook_bench 1000000`, `-O2`):

| Metric         | Value          |
| -------------- | -------------- |
| Operations     | 1,000,000      |
| Throughput     | ~7.9 M ops/s   |
| ADD latency    | p50 ~100 ns, p99 ~600 ns |
| CANCEL latency | p50 ~100 ns, p99 ~700 ns |

Numbers vary by machine; the first operation pays a one-time
page-commit / cache warmup cost that shows up only in the `max` figure.

## Data structures

The book uses:

- A contiguous order pool (`std::vector<OrderNode>`) with a free list.
- A flat `std::vector<PriceLevel>` indexed by price, with `best_bid_` /
  `best_ask_` cursors.
- `std::unordered_map<int, int>` mapping order id to pool index for O(1)
  cancellation.

Bids match highest price first, asks lowest price first, and orders at the
same price match FIFO, giving price-time priority.

## Possible next optimizations

- Replace `std::unordered_map` order index with an open-addressing map.
- Add SIMD / batch parsing for very large replay files.
- Add a custom slab allocator tuned to cache line size.
- Multi-instrument support with per-symbol books.

## Resume bullet

Built a low-latency C++ limit order book and matching engine supporting
price-time priority, order cancellation, file-based event replay, trade
generation, and nanosecond-level p50/p99 latency reporting; sustains
~7.9M ops/s with ~100 ns median add latency on a 1M-operation benchmark.
