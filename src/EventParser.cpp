#include "EventParser.h"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace {

// ASCII case-insensitive compare between a token and a fixed keyword.
bool iequals(std::string_view token, std::string_view keyword) {
    if (token.size() != keyword.size()) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        char a = token[i];
        char b = keyword[i];
        if (a >= 'a' && a <= 'z') {
            a = static_cast<char>(a - 'a' + 'A');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

// Lightweight whitespace tokenizer over a string_view: no copies, no streams.
class Tokenizer {
public:
    explicit Tokenizer(std::string_view text) : text_(text) {}

    bool next(std::string_view& out) {
        while (pos_ < text_.size() && is_space(text_[pos_])) {
            ++pos_;
        }
        if (pos_ >= text_.size()) {
            return false;
        }
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !is_space(text_[pos_])) {
            ++pos_;
        }
        out = text_.substr(start, pos_ - start);
        return true;
    }

private:
    static bool is_space(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    std::string_view text_;
    std::size_t pos_{0};
};

int parse_int(std::string_view token, int line_number, const char* field) {
    int value = 0;
    const char* begin = token.data();
    const char* end = begin + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        throw std::runtime_error("Line " + std::to_string(line_number) +
                                 ": invalid integer for " + field + ": " +
                                 std::string(token));
    }
    return value;
}

Side parse_side(std::string_view token, int line_number) {
    if (iequals(token, "BUY") || iequals(token, "B")) {
        return Side::Buy;
    }
    if (iequals(token, "SELL") || iequals(token, "S")) {
        return Side::Sell;
    }
    throw std::runtime_error("Line " + std::to_string(line_number) +
                             ": invalid side: " + std::string(token));
}

}  // namespace

std::optional<Event> EventParser::parse_line(const std::string& line, int line_number) {
    Tokenizer tokenizer(line);

    std::string_view command;
    if (!tokenizer.next(command)) {
        return std::nullopt;  // blank or whitespace-only line
    }

    if (command.front() == '#') {
        return std::nullopt;  // comment line
    }

    if (iequals(command, "ADD")) {
        std::string_view id_token;
        std::string_view side_token;
        std::string_view price_token;
        std::string_view qty_token;

        if (!tokenizer.next(id_token) || !tokenizer.next(side_token) ||
            !tokenizer.next(price_token) || !tokenizer.next(qty_token)) {
            throw std::runtime_error("Line " + std::to_string(line_number) +
                                     ": expected ADD <id> <BUY/SELL> <price> <quantity>");
        }

        Event event;
        event.type = EventType::Add;
        event.order = Order{
            parse_int(id_token, line_number, "id"),
            parse_side(side_token, line_number),
            parse_int(price_token, line_number, "price"),
            parse_int(qty_token, line_number, "quantity")
        };
        return event;
    }

    if (iequals(command, "CANCEL")) {
        std::string_view id_token;
        if (!tokenizer.next(id_token)) {
            throw std::runtime_error("Line " + std::to_string(line_number) +
                                     ": expected CANCEL <id>");
        }

        Event event;
        event.type = EventType::Cancel;
        event.cancel_order_id = parse_int(id_token, line_number, "id");
        return event;
    }

    if (iequals(command, "PRINT")) {
        Event event;
        event.type = EventType::Print;
        return event;
    }

    throw std::runtime_error("Line " + std::to_string(line_number) +
                             ": unknown command: " + std::string(command));
}
