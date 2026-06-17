cmake_minimum_required(VERSION 3.15)

project(LowLatencyOrderBook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(orderbook
    src/main.cpp
    src/OrderBook.cpp
    src/EventParser.cpp
    src/LatencyStats.cpp
)

enable_testing()

add_executable(orderbook_tests
    tests/test_orderbook.cpp
    src/OrderBook.cpp
)
target_include_directories(orderbook_tests PRIVATE src)

add_test(NAME orderbook_tests COMMAND orderbook_tests)

add_executable(orderbook_bench
    tools/benchmark.cpp
    src/OrderBook.cpp
    src/LatencyStats.cpp
)
target_include_directories(orderbook_bench PRIVATE src)
