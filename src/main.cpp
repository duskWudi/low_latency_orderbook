#include "EventParser.h"
#include "LatencyStats.h"
#include "OrderBook.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
const char* demo_input = R"(
# Format:
# ADD <id> <BUY/SELL> <price> <quantity>
# CANCEL <id>
# PRINT

ADD 1 BUY 10000 10
ADD 2 BUY 10100 5
ADD 3 SELL 9900 7
ADD 4 SELL 10200 3
CANCEL 4
PRINT
)";

long long elapsed_nanoseconds(
    const std::chrono::high_resolution_clock::time_point& start,
    const std::chrono::high_resolution_clock::time_point& end
) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        OrderBook book;
        LatencyStats latency_stats;

        std::ifstream file;
        std::istringstream demo_stream(demo_input);
        std::istream* input = &demo_stream;

        if (argc >= 2) {
            file.open(argv[1]);
            if (!file) {
                throw std::runtime_error("Could not open input file: " + std::string(argv[1]));
            }
            input = &file;
        } else {
            std::cout << "No input file provided. Running built-in demo.\n";
            std::cout << "To use your own file: ./orderbook data/sample_orders.txt\n\n";
        }

        std::string line;
        int line_number = 0;
        std::vector<Trade> trades;  // reused across orders to avoid per-add allocation

        while (std::getline(*input, line)) {
            ++line_number;

            auto event = EventParser::parse_line(line, line_number);
            if (!event.has_value()) {
                continue;
            }

            if (event->type == EventType::Add) {
                const auto start = std::chrono::high_resolution_clock::now();
                book.add_order(event->order, trades);
                const auto end = std::chrono::high_resolution_clock::now();

                latency_stats.record(elapsed_nanoseconds(start, end));

                std::cout << "ADD id=" << event->order.id
                          << " side=" << to_string(event->order.side)
                          << " price=" << event->order.price
                          << " qty=" << event->order.quantity << "\n";

                for (const Trade& trade : trades) {
                    std::cout << trade << "\n";
                }
            } else if (event->type == EventType::Cancel) {
                const auto start = std::chrono::high_resolution_clock::now();
                const bool cancelled = book.cancel_order(event->cancel_order_id);
                const auto end = std::chrono::high_resolution_clock::now();

                latency_stats.record(elapsed_nanoseconds(start, end));

                std::cout << "CANCEL id=" << event->cancel_order_id
                          << " result=" << (cancelled ? "OK" : "NOT_FOUND") << "\n";
            } else if (event->type == EventType::Print) {
                book.print_book(std::cout);
            }
        }

        latency_stats.print_report(std::cout, "LATENCY REPORT");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
