# Low-Latency C++ Limit Order Book

This project implements a simplified C++ limit order book and matching engine.
It supports limit orders, price-time priority, cancellation, trade generation,
file-based event replay, and nanosecond-level latency measurement.

## Why this project matters

This is a trading-systems project, not a fake trading bot. It demonstrates:

- C++ object-oriented design
- Trading market structure
- Price-time priority matching
- Data structure choices
- Latency measurement
- Clean command parsing
- Practical systems programming

## Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Run demo

```bash
./orderbook
```

## Run with sample data

From the `build` directory:

```bash
./orderbook ../data/sample_orders.txt
```

## Input format

```txt
ADD <id> <BUY/SELL> <price> <quantity>
CANCEL <id>
PRINT
```

Example:

```txt
ADD 1 BUY 10000 10
ADD 2 BUY 10100 5
ADD 3 SELL 9900 7
CANCEL 1
PRINT
```

Prices are integers. For example, 10000 can mean $100.00.
This avoids floating-point precision problems.

## Current design

- `OrderBook` owns the matching logic.
- `EventParser` converts text commands into typed events.
- `LatencyStats` records and reports operation latency.
- `Order`, `Trade`, and `Side` model the core trading objects.

The book uses:

- `std::map<int, std::list<Order>, std::greater<int>>` for bids
- `std::map<int, std::list<Order>>` for asks
- `std::unordered_map<int, OrderLocation>` for fast cancellation

Bids are sorted highest price first.
Asks are sorted lowest price first.
Orders at the same price level are matched FIFO, which gives price-time priority.

## Possible next optimizations

- Replace `std::map` with price-level arrays when the price range is known.
- Preallocate memory to reduce heap allocation.
- Add a custom memory pool for orders.
- Add a synthetic event generator for 1M+ order benchmark runs.
- Separate benchmark mode from verbose printing mode.

## Resume bullet

Built a low-latency C++ limit order book and matching engine supporting price-time priority,
order cancellation, file-based event replay, trade generation, and nanosecond-level p50/p99
latency reporting.
