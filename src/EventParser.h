#pragma once

#include "Order.h"

#include <optional>
#include <string>

// Supported input commands:
// ADD <id> <BUY/SELL> <price> <quantity>
// CANCEL <id>
// PRINT
// Lines beginning with # are ignored.

enum class EventType {
    Add,
    Cancel,
    Print
};

struct Event {
    EventType type{EventType::Print};
    Order order{};
    int cancel_order_id{};
};

class EventParser {
public:
    static std::optional<Event> parse_line(const std::string& line, int line_number);
};
