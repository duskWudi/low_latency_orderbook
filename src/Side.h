#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>

// Trading side: either buying from the book or selling into the book.
enum class Side {
    Buy,
    Sell
};

inline std::string to_string(Side side) {
    return side == Side::Buy ? "BUY" : "SELL";
}

inline Side side_from_string(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    if (text == "BUY" || text == "B") {
        return Side::Buy;
    }
    if (text == "SELL" || text == "S") {
        return Side::Sell;
    }

    throw std::invalid_argument("Invalid side: " + text);
}
