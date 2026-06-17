#include "LatencyStats.h"
#include "Order.h"
#include "OrderBook.h"
#include "Side.h"
#include "Trade.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

// Synthetic benchmark for the matching engine.
//
// Generates a deterministic workload of ADD / CANCEL operations centered on a
// single price band (so the book actually crosses and trades), then replays it
// twice: once for raw throughput, once to gather per-operation latency.
//
// Usage: orderbook_bench [num_operations]

namespace {

struct Op {
    bool is_add{true};
    Order order{};
    int cancel_id{0};
};

// xorshift64: fast, deterministic, good enough for workload generation.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_;
    }

private:
    std::uint64_t state_;
};

long long elapsed_ns(
    std::chrono::high_resolution_clock::time_point start,
    std::chrono::high_resolution_clock::time_point end
) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}

std::vector<Op> generate_workload(int n, int mid) {
    Rng rng(88172645463325252ull);
    std::vector<Op> ops;
    ops.reserve(static_cast<std::size_t>(n));

    int next_id = 1;
    for (int i = 0; i < n; ++i) {
        const std::uint64_t r = rng.next();
        const bool do_cancel = (r % 100) < 15 && next_id > 100;

        if (do_cancel) {
            const int victim = 1 + static_cast<int>(rng.next() % static_cast<std::uint64_t>(next_id - 1));
            ops.push_back(Op{false, Order{}, victim});
        } else {
            const Side side = (r & 1) ? Side::Buy : Side::Sell;
            const int price = mid + static_cast<int>(rng.next() % 21) - 10;
            const int qty = 1 + static_cast<int>(rng.next() % 10);
            ops.push_back(Op{true, Order{next_id++, side, price, qty}, 0});
        }
    }
    return ops;
}

long long replay_plain(OrderBook& book, const std::vector<Op>& ops, std::vector<Trade>& trades) {
    long long total_trades = 0;
    for (const Op& op : ops) {
        if (op.is_add) {
            book.add_order(op.order, trades);
            total_trades += static_cast<long long>(trades.size());
        } else {
            book.cancel_order(op.cancel_id);
        }
    }
    return total_trades;
}

void replay_with_latency(
    OrderBook& book,
    const std::vector<Op>& ops,
    std::vector<Trade>& trades,
    LatencyStats& add_stats,
    LatencyStats& cancel_stats
) {
    for (const Op& op : ops) {
        if (op.is_add) {
            const auto s = std::chrono::high_resolution_clock::now();
            book.add_order(op.order, trades);
            const auto e = std::chrono::high_resolution_clock::now();
            add_stats.record(elapsed_ns(s, e));
        } else {
            const auto s = std::chrono::high_resolution_clock::now();
            book.cancel_order(op.cancel_id);
            const auto e = std::chrono::high_resolution_clock::now();
            cancel_stats.record(elapsed_ns(s, e));
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    int n = 1'000'000;
    if (argc >= 2) {
        n = std::atoi(argv[1]);
        if (n <= 0) {
            n = 1'000'000;
        }
    }

    const int mid = 10000;
    const int max_price = 20000;

    std::cout << "Generating " << n << " operations...\n";
    const std::vector<Op> ops = generate_workload(n, mid);

    std::vector<Trade> trades;

    // Pass 1: raw throughput (no per-op instrumentation).
    OrderBook throughput_book(max_price);
    const auto wall_start = std::chrono::high_resolution_clock::now();
    const long long total_trades = replay_plain(throughput_book, ops, trades);
    const auto wall_end = std::chrono::high_resolution_clock::now();

    const double seconds = static_cast<double>(elapsed_ns(wall_start, wall_end)) / 1e9;
    const double mops = (static_cast<double>(ops.size()) / seconds) / 1e6;

    // Pass 2: per-operation latency on a fresh book with the same workload.
    OrderBook latency_book(max_price);
    LatencyStats add_stats;
    LatencyStats cancel_stats;
    replay_with_latency(latency_book, ops, trades, add_stats, cancel_stats);

    std::cout << "\n===== BENCHMARK =====\n";
    std::cout << "operations : " << ops.size() << "\n";
    std::cout << "trades     : " << total_trades << "\n";
    std::cout << "resting    : " << throughput_book.resting_order_count() << "\n";
    std::cout << "wall time  : " << seconds << " s\n";
    std::cout << "throughput : " << mops << " M ops/s\n";
    std::cout << "=====================\n";

    add_stats.print_report(std::cout, "ADD LATENCY (ns)");
    cancel_stats.print_report(std::cout, "CANCEL LATENCY (ns)");
    return 0;
}
