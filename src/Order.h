#pragma once

#include "Side.h"

// One limit order entered into the matching engine.
// price is stored as an integer to avoid floating-point errors.
// Example: $101.25 can be stored as 10125 cents.
struct Order {
    int id{};
    Side side{Side::Buy};
    int price{};
    int quantity{};

    bool is_valid() const {
        return id > 0 && price > 0 && quantity > 0;
    }
};
