#pragma once

#include <ostream>

// A trade happens when an incoming order matches a resting order.
struct Trade {
    int buy_order_id{};
    int sell_order_id{};
    int price{};
    int quantity{};
};

inline std::ostream& operator<<(std::ostream& os, const Trade& trade) {
    os << "TRADE buyer=" << trade.buy_order_id
       << " seller=" << trade.sell_order_id
       << " price=" << trade.price
       << " qty=" << trade.quantity;
    return os;
}
