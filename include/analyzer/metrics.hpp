#pragma once
#include "analyzer/analyzer.hpp"

namespace analyzer {
struct Metrics {
    double seconds{};
    double cpu_seconds{};
    std::uint64_t peak_resident_bytes{};
    std::uint64_t page_faults{};
};
struct Measurement {
    Output analysis;
    Metrics metrics;
};
Measurement measure(const std::filesystem::path& path, Mode mode, unsigned workers,
                    std::size_t buffer_size);
double throughput(const Measurement& measurement) noexcept;
}
