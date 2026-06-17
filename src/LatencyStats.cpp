#include "LatencyStats.h"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <ostream>

void LatencyStats::record(long long nanoseconds) {
    samples_.push_back(nanoseconds);
}

bool LatencyStats::empty() const {
    return samples_.empty();
}

void LatencyStats::print_report(std::ostream& os, const std::string& title) const {
    if (samples_.empty()) {
        os << title << ": no latency samples\n";
        return;
    }

    std::vector<long long> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    const long long sum = std::accumulate(sorted.begin(), sorted.end(), 0LL);
    const double average = static_cast<double>(sum) / static_cast<double>(sorted.size());

    os << "\n===== " << title << " =====\n";
    os << "samples: " << sorted.size() << "\n";
    os << "avg ns : " << std::fixed << std::setprecision(2) << average << "\n";
    os << "p50 ns : " << percentile(sorted, 50.0) << "\n";
    os << "p99 ns : " << percentile(sorted, 99.0) << "\n";
    os << "min ns : " << sorted.front() << "\n";
    os << "max ns : " << sorted.back() << "\n";
    os << "==============================\n";
}

long long LatencyStats::percentile(const std::vector<long long>& sorted_samples, double percentile_value) {
    if (sorted_samples.empty()) {
        return 0;
    }

    const double rank = percentile_value / 100.0 * static_cast<double>(sorted_samples.size() - 1);
    const auto index = static_cast<std::size_t>(rank);
    return sorted_samples[index];
}
