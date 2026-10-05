#pragma once
#include "analyzer/metrics.hpp"
#include <string>
#include <utility>
#include <vector>

namespace analyzer {
struct BenchmarkOptions {
    std::filesystem::path input;
    std::filesystem::path csv_output{"results/benchmark.csv"};
    std::vector<unsigned> threads{1, 2, 4};
    unsigned runs{7};
    std::size_t buffer_size{default_buffer_size};
    std::uint64_t order_seed{42};
};
struct Configuration { Mode mode; unsigned threads; };
struct Trial { Configuration configuration; unsigned round; Measurement measurement; };
struct Summary {
    Configuration configuration;
    unsigned actual_workers{};
    double median_seconds{}, iqr_seconds{}, mib_per_second{}, speedup{}, mapped_scaling{};
    double median_cpu_seconds{}, median_peak_mib{}, median_faults{};
};
struct BenchmarkResult {
    AnalysisResult reference;
    std::vector<Trial> trials;
    std::vector<Summary> summaries;
    std::vector<std::pair<std::string, std::string>> metadata;
};
double quantile(std::vector<double> values, double fraction);
void write_trial(const std::filesystem::path& path, const Measurement& measurement);
BenchmarkResult run_benchmark(const BenchmarkOptions& options);
void write_reports(const BenchmarkOptions& options, const BenchmarkResult& result);
void print_comparison(const BenchmarkResult& result);
}
