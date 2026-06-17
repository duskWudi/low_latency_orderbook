#pragma once

#include "Order.h"
#include "Trade.h"

#include <cstddef>
#include <ostream>
#include <unordered_map>
#include <vector>

// Low-latency limit order book.
//
// Two structural choices remove per-operation heap allocation and the
// pointer chasing of node-based containers:
//
//  1. Order memory pool: all resting orders live in one contiguous
//     std::vector<OrderNode>. Nodes are recycled through a free list, so
//     add/cancel never call the global allocator after warmup.
//  2. Flat array price levels: levels_ is indexed directly by price, giving
//     O(1) access to any level. best_bid_ / best_ask_ cursors track the top
//     of book so matching never searches a tree.
//
// Orders at the same price are kept in an intrusive doubly linked list
// (prev/next indices into the pool), which preserves FIFO price-time
// priority and allows O(1) cancellation.
class OrderBook {
public:
    // max_price bounds the price axis (prices are integers, e.g. cents).
    // Orders priced above max_price are rejected.
    explicit OrderBook(int max_price = 1'000'000);

    // Convenience overload: allocates and returns the trade list.
    std::vector<Trade> add_order(const Order& order);

    // Hot-path overload: appends generated trades into a caller-owned buffer
    // (cleared first), so a replay loop can reuse one allocation across orders.
    void add_order(const Order& order, std::vector<Trade>& trades_out);

    bool cancel_order(int order_id);
    void print_book(std::ostream& os, int max_levels = 10) const;
    std::size_t resting_order_count() const;

private:
    struct OrderNode {
        int id{};
        int price{};
        int quantity{};
        Side side{Side::Buy};
        int prev{-1};
        int next{-1};
    };

    struct PriceLevel {
        int head{-1};  // oldest resting order (FIFO front)
        int tail{-1};  // newest resting order (push position)
    };

    int max_price_;
    int num_levels_;  // max_price_ + 1, so price is a valid index
    std::vector<PriceLevel> levels_;
    std::vector<OrderNode> nodes_;
    int free_head_{-1};  // head of the recycled-node free list
    std::unordered_map<int, int> order_index_;  // order id -> pool node index

    int best_bid_;  // highest active bid price, -1 if no bids
    int best_ask_;  // lowest active ask price, num_levels_ if no asks

    int alloc_node(const Order& order);
    void free_node(int node_idx);
    void link_back(PriceLevel& level, int node_idx);
    void unlink(int node_idx);

    void validate_new_order(const Order& order) const;
    void rest_order(const Order& order);
    void match_buy(Order& incoming, std::vector<Trade>& trades);
    void match_sell(Order& incoming, std::vector<Trade>& trades);

    void advance_best_bid();
    void advance_best_ask();
};
