#pragma once

#include <iosfwd>
#include <string>
#include <vector>

class LatencyStats {
public:
    void record(long long nanoseconds);
    bool empty() const;
    void print_report(std::ostream& os, const std::string& title) const;

private:
    std::vector<long long> samples_;

    // Expects sorted_samples to already be sorted ascending.
    static long long percentile(const std::vector<long long>& sorted_samples, double percentile_value);
};
