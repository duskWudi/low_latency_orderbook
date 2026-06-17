#include "OrderBook.h"

#include <algorithm>
#include <iomanip>
#include <stdexcept>

OrderBook::OrderBook(int max_price)
    : max_price_(max_price),
      num_levels_(max_price + 1),
      levels_(static_cast<std::size_t>(num_levels_)),
      best_bid_(-1),
      best_ask_(num_levels_) {
    if (max_price <= 0) {
        throw std::invalid_argument("max_price must be positive");
    }
    nodes_.reserve(1024);
}

int OrderBook::alloc_node(const Order& order) {
    int idx;
    if (free_head_ != -1) {
        idx = free_head_;
        free_head_ = nodes_[idx].next;
    } else {
        idx = static_cast<int>(nodes_.size());
        nodes_.push_back(OrderNode{});
    }

    OrderNode& node = nodes_[idx];
    node.id = order.id;
    node.price = order.price;
    node.quantity = order.quantity;
    node.side = order.side;
    node.prev = -1;
    node.next = -1;
    return idx;
}

void OrderBook::free_node(int node_idx) {
    // Reuse the next field as the free-list link.
    nodes_[node_idx].next = free_head_;
    free_head_ = node_idx;
}

void OrderBook::link_back(PriceLevel& level, int node_idx) {
    nodes_[node_idx].prev = level.tail;
    nodes_[node_idx].next = -1;
    if (level.tail != -1) {
        nodes_[level.tail].next = node_idx;
    } else {
        level.head = node_idx;
    }
    level.tail = node_idx;
}

void OrderBook::unlink(int node_idx) {
    const OrderNode& node = nodes_[node_idx];
    PriceLevel& level = levels_[node.price];

    if (node.prev != -1) {
        nodes_[node.prev].next = node.next;
    } else {
        level.head = node.next;
    }

    if (node.next != -1) {
        nodes_[node.next].prev = node.prev;
    } else {
        level.tail = node.prev;
    }
}

std::vector<Trade> OrderBook::add_order(const Order& order) {
    std::vector<Trade> trades;
    add_order(order, trades);
    return trades;
}

void OrderBook::add_order(const Order& order, std::vector<Trade>& trades_out) {
    validate_new_order(order);

    trades_out.clear();
    Order incoming = order;

    if (incoming.side == Side::Buy) {
        match_buy(incoming, trades_out);
    } else {
        match_sell(incoming, trades_out);
    }

    if (incoming.quantity > 0) {
        rest_order(incoming);
    }
}

bool OrderBook::cancel_order(int order_id) {
    auto index_it = order_index_.find(order_id);
    if (index_it == order_index_.end()) {
        return false;
    }

    const int node_idx = index_it->second;
    const Side side = nodes_[node_idx].side;

    unlink(node_idx);
    free_node(node_idx);
    order_index_.erase(index_it);

    // If the top of book emptied, slide the cursor to the next live level.
    if (side == Side::Buy) {
        advance_best_bid();
    } else {
        advance_best_ask();
    }

    return true;
}

void OrderBook::print_book(std::ostream& os, int max_levels) const {
    os << "\n===== ORDER BOOK =====\n";

    os << "ASKS lowest first:\n";
    int printed = 0;
    for (int price = best_ask_; price < num_levels_ && printed < max_levels; ++price) {
        const PriceLevel& level = levels_[price];
        if (level.head == -1) {
            continue;
        }

        long long total_quantity = 0;
        int order_count = 0;
        for (int idx = level.head; idx != -1; idx = nodes_[idx].next) {
            total_quantity += nodes_[idx].quantity;
            ++order_count;
        }

        os << "  price=" << std::setw(6) << price
           << " qty=" << std::setw(6) << total_quantity
           << " orders=" << order_count << "\n";
        ++printed;
    }

    os << "BIDS highest first:\n";
    printed = 0;
    for (int price = best_bid_; price >= 0 && printed < max_levels; --price) {
        const PriceLevel& level = levels_[price];
        if (level.head == -1) {
            continue;
        }

        long long total_quantity = 0;
        int order_count = 0;
        for (int idx = level.head; idx != -1; idx = nodes_[idx].next) {
            total_quantity += nodes_[idx].quantity;
            ++order_count;
        }

        os << "  price=" << std::setw(6) << price
           << " qty=" << std::setw(6) << total_quantity
           << " orders=" << order_count << "\n";
        ++printed;
    }

    os << "Resting orders: " << resting_order_count() << "\n";
    os << "======================\n\n";
}

std::size_t OrderBook::resting_order_count() const {
    return order_index_.size();
}

void OrderBook::validate_new_order(const Order& order) const {
    if (!order.is_valid()) {
        throw std::invalid_argument("Order must have positive id, price, and quantity");
    }

    if (order.price > max_price_) {
        throw std::invalid_argument("Order price exceeds book max_price: " +
                                    std::to_string(order.price));
    }

    if (order_index_.count(order.id) > 0) {
        throw std::invalid_argument("Duplicate order id: " + std::to_string(order.id));
    }
}

void OrderBook::rest_order(const Order& order) {
    const int node_idx = alloc_node(order);
    link_back(levels_[order.price], node_idx);
    order_index_[order.id] = node_idx;

    if (order.side == Side::Buy) {
        if (order.price > best_bid_) {
            best_bid_ = order.price;
        }
    } else {
        if (order.price < best_ask_) {
            best_ask_ = order.price;
        }
    }
}

void OrderBook::match_buy(Order& incoming, std::vector<Trade>& trades) {
    // A buy crosses asks at or below its limit, cheapest first.
    while (incoming.quantity > 0 && best_ask_ < num_levels_ && best_ask_ <= incoming.price) {
        PriceLevel& level = levels_[best_ask_];
        const int node_idx = level.head;
        OrderNode& resting_sell = nodes_[node_idx];

        const int traded_quantity = std::min(incoming.quantity, resting_sell.quantity);
        trades.push_back(Trade{
            incoming.id,
            resting_sell.id,
            resting_sell.price,
            traded_quantity
        });

        incoming.quantity -= traded_quantity;
        resting_sell.quantity -= traded_quantity;

        if (resting_sell.quantity == 0) {
            order_index_.erase(resting_sell.id);
            unlink(node_idx);
            free_node(node_idx);
            if (level.head == -1) {
                advance_best_ask();
            }
        }
    }
}

void OrderBook::match_sell(Order& incoming, std::vector<Trade>& trades) {
    // A sell crosses bids at or above its limit, highest first.
    while (incoming.quantity > 0 && best_bid_ >= 0 && best_bid_ >= incoming.price) {
        PriceLevel& level = levels_[best_bid_];
        const int node_idx = level.head;
        OrderNode& resting_buy = nodes_[node_idx];

        const int traded_quantity = std::min(incoming.quantity, resting_buy.quantity);
        trades.push_back(Trade{
            resting_buy.id,
            incoming.id,
            resting_buy.price,
            traded_quantity
        });

        incoming.quantity -= traded_quantity;
        resting_buy.quantity -= traded_quantity;

        if (resting_buy.quantity == 0) {
            order_index_.erase(resting_buy.id);
            unlink(node_idx);
            free_node(node_idx);
            if (level.head == -1) {
                advance_best_bid();
            }
        }
    }
}

void OrderBook::advance_best_bid() {
    while (best_bid_ >= 0 && levels_[best_bid_].head == -1) {
        --best_bid_;
    }
}

void OrderBook::advance_best_ask() {
    while (best_ask_ < num_levels_ && levels_[best_ask_].head == -1) {
        ++best_ask_;
    }
}
