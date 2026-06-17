#include "OrderBook.h"
#include "Order.h"
#include "Side.h"
#include "Trade.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cerr << "  [FAIL] " << what << "\n";
    }
}

Order buy(int id, int price, int qty) {
    return Order{id, Side::Buy, price, qty};
}

Order sell(int id, int price, int qty) {
    return Order{id, Side::Sell, price, qty};
}

bool trade_eq(const Trade& t, int buyer, int seller, int price, int qty) {
    return t.buy_order_id == buyer && t.sell_order_id == seller &&
           t.price == price && t.quantity == qty;
}

// A buy that does not cross simply rests.
void test_rest_no_match() {
    OrderBook book;
    auto trades = book.add_order(buy(1, 10000, 10));
    check(trades.empty(), "rest_no_match: no trades on resting buy");
    check(book.resting_order_count() == 1, "rest_no_match: one resting order");
}

// A crossing buy lifts the cheapest ask first (price priority).
void test_price_priority() {
    OrderBook book;
    book.add_order(sell(1, 10100, 5));
    book.add_order(sell(2, 10000, 5));  // cheaper, should fill first

    auto trades = book.add_order(buy(3, 10100, 5));
    check(trades.size() == 1, "price_priority: one trade");
    check(trade_eq(trades[0], 3, 2, 10000, 5),
          "price_priority: cheapest ask (id 2 @ 10000) filled first");
    check(book.resting_order_count() == 1, "price_priority: one ask remains");
}

// Orders at the same price fill oldest-first (time priority / FIFO).
void test_time_priority_fifo() {
    OrderBook book;
    book.add_order(buy(1, 10000, 5));  // older
    book.add_order(buy(2, 10000, 5));  // newer

    auto trades = book.add_order(sell(3, 10000, 5));
    check(trades.size() == 1, "fifo: one trade");
    check(trade_eq(trades[0], 1, 3, 10000, 5),
          "fifo: oldest bid (id 1) filled first");
}

// An incoming order can sweep several resting orders and partially fill.
void test_partial_and_sweep() {
    OrderBook book;
    book.add_order(sell(1, 10000, 3));
    book.add_order(sell(2, 10000, 4));

    auto trades = book.add_order(buy(3, 10000, 10));  // wants 10, only 7 available
    check(trades.size() == 2, "sweep: two trades");
    check(trade_eq(trades[0], 3, 1, 10000, 3), "sweep: first fill id1 qty3");
    check(trade_eq(trades[1], 3, 2, 10000, 4), "sweep: second fill id2 qty4");
    // Remaining 3 rests as a bid.
    check(book.resting_order_count() == 1, "sweep: remainder rests");

    std::ostringstream os;
    book.print_book(os);
    check(os.str().find("price= 10000 qty=     3 orders=1") != std::string::npos,
          "sweep: remaining bid of qty 3 visible");
}

// Cancel removes a resting order and updates the top of book.
void test_cancel() {
    OrderBook book;
    book.add_order(buy(1, 10000, 5));
    book.add_order(buy(2, 10100, 5));  // best bid

    check(book.cancel_order(2), "cancel: existing order returns true");
    check(!book.cancel_order(999), "cancel: missing order returns false");
    check(book.resting_order_count() == 1, "cancel: one order left");

    // After cancelling the best bid (10100), a sell at 10000 must hit id 1.
    auto trades = book.add_order(sell(3, 10000, 5));
    check(trades.size() == 1 && trade_eq(trades[0], 1, 3, 10000, 5),
          "cancel: best-bid cursor slid down to 10000");
    check(book.resting_order_count() == 0, "cancel: book empty after fill");
}

// Duplicate ids and out-of-range prices are rejected.
void test_validation() {
    OrderBook book(100000);
    book.add_order(buy(1, 10000, 5));

    bool threw = false;
    try {
        book.add_order(buy(1, 9000, 1));  // duplicate id
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "validation: duplicate id throws");

    threw = false;
    try {
        book.add_order(buy(2, 200000, 1));  // above max_price
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "validation: price above max throws");
}

// Reproduce the shipped sample_orders.txt scenario end to end.
void test_sample_scenario() {
    OrderBook book;

    book.add_order(buy(1, 10000, 10));
    book.add_order(buy(2, 10100, 5));

    auto t3 = book.add_order(sell(3, 9900, 7));
    check(t3.size() == 2, "sample: sell 3 makes two trades");
    check(trade_eq(t3[0], 2, 3, 10100, 5), "sample: id2 filled first @10100");
    check(trade_eq(t3[1], 1, 3, 10000, 2), "sample: id1 filled next @10000");

    book.add_order(sell(4, 10200, 3));
    auto t5 = book.add_order(buy(5, 10200, 2));
    check(t5.size() == 1 && trade_eq(t5[0], 5, 4, 10200, 2),
          "sample: buy 5 lifts ask 4 @10200");

    check(book.cancel_order(4), "sample: cancel 4 ok");
    check(book.resting_order_count() == 1, "sample: one resting order (id1)");

    std::ostringstream os;
    book.print_book(os);
    check(os.str().find("price= 10000 qty=     8 orders=1") != std::string::npos,
          "sample: order 1 rests with qty 8 @10000");
}

// Allocate and free many orders to exercise the pool free list.
void test_stress_pool_reuse() {
    OrderBook book;
    const int n = 5000;

    for (int i = 1; i <= n; ++i) {
        book.add_order(buy(i, 10000 + (i % 50), 1));
    }
    check(book.resting_order_count() == static_cast<std::size_t>(n),
          "stress: all orders resting");

    for (int i = 1; i <= n; ++i) {
        check(book.cancel_order(i), "stress: cancel each order");
    }
    check(book.resting_order_count() == 0, "stress: book empty after cancels");

    // Re-add after freeing everything; the pool should recycle nodes.
    auto trades = book.add_order(buy(n + 1, 10000, 1));
    check(trades.empty() && book.resting_order_count() == 1,
          "stress: reuse pool after full drain");
}

}  // namespace

int main() {
    test_rest_no_match();
    test_price_priority();
    test_time_priority_fifo();
    test_partial_and_sweep();
    test_cancel();
    test_validation();
    test_sample_scenario();
    test_stress_pool_reuse();

    std::cout << "\nChecks run: " << g_checks
              << ", failures: " << g_failures << "\n";
    if (g_failures == 0) {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }
    std::cout << "TESTS FAILED\n";
    return 1;
}
