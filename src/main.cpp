#include "analyzer/benchmark.hpp"
#include "analyzer/dataset_generator.hpp"
#include "analyzer/text.hpp"
#include <charconv>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <shellapi.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Options = std::map<std::string, std::string>;

std::vector<std::string> arguments() {
    int count = 0;
    auto raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw) throw std::runtime_error("Cannot parse command line");
    std::vector<std::string> result;
    try { for (int i = 0; i < count; ++i) result.push_back(analyzer::utf8(raw[i])); }
    catch (...) { LocalFree(raw); throw; }
    LocalFree(raw);
    return result;
}

Options parse_options(const std::vector<std::string>& args, std::size_t start,
                      const std::set<std::string>& allowed, const std::set<std::string>& flags = {}) {
    Options options;
    for (std::size_t i = start; i < args.size(); ++i) {
        const auto& name = args[i];
        if (!allowed.contains(name) && !flags.contains(name)) throw std::invalid_argument("Unknown option: " + name);
        if (options.contains(name)) throw std::invalid_argument("Duplicate option: " + name);
        if (flags.contains(name)) { options[name] = "true"; continue; }
        if (i + 1 >= args.size() || args[i + 1].starts_with("--")) throw std::invalid_argument("Missing value for " + name);
        options[name] = args[++i];
    }
    return options;
}

std::string option(const Options& options, const std::string& name, const std::string& fallback) {
    const auto item = options.find(name);
    return item == options.end() ? fallback : item->second;
}

std::uint64_t number(const std::string& value, std::uint64_t maximum, bool allow_zero = false) {
    std::uint64_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || (!allow_zero && result == 0) || result > maximum) {
        throw std::invalid_argument("Invalid number: " + value);
    }
    return result;
}

std::filesystem::path path(const std::string& value) {
    if (value.empty()) throw std::invalid_argument("Path must not be empty");
    return analyzer::wide(value);
}

unsigned workers(const Options& options, unsigned fallback) {
    return static_cast<unsigned>(number(option(options, "--threads", std::to_string(fallback)), analyzer::max_workers));
}
std::size_t buffer_size(const Options& options) {
    return static_cast<std::size_t>(number(option(options, "--buffer-kib", "1024"), 65536) * 1024);
}
std::vector<unsigned> thread_list(const std::string& list) {
    std::vector<unsigned> result;
    std::size_t start = 0;
    while (true) {
        const auto end = list.find(',', start);
        result.push_back(static_cast<unsigned>(number(list.substr(start, end - start), analyzer::max_workers)));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

void help() {
    std::cout << "High-Performance Memory-Mapped Concurrent File Analyzer\n\n"
              << "  file_analyzer generate --output FILE --size-mib N [--seed N] [--force]\n"
              << "  file_analyzer generate --output FILE --size-bytes N [--seed N] [--force]\n"
              << "  file_analyzer analyze FILE [--mode MODE] [--threads N] [--buffer-kib N]\n"
              << "  file_analyzer benchmark FILE [--threads 1,2,4] [--runs 7]\n"
              << "                          [--buffer-kib 1024] [--order-seed 42]\n"
              << "                          [--output results/benchmark.csv]\n\n"
              << "Modes: buffered, mapped, mapped-parallel. Analyze defaults to mapped-parallel.\n"
              << "Words: ASCII letters/digits/underscore. Keywords: exact ERROR, WARN, INFO.\n"
              << "Lines: LF/CRLF; a final nonempty line without LF counts. Empty file: 0.\n"
              << "Benchmark: shuffled configurations, fresh process per trial, pre-read cache;\n"
              << "reports raw CSV, summary CSV, Markdown, and environment metadata.\n"
              << "Keep input unchanged. --threads range: 1..256; --runs: 1..100.\n"
              << "Dataset generation refuses an existing output unless --force is supplied.\n";
}

int execute(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1] == "--help" || args[1] == "help") { help(); return 0; }
    const auto& command = args[1];
    if (command == "generate") {
        const auto options = parse_options(args, 2, {"--output", "--size-mib", "--size-bytes", "--seed"}, {"--force"});
        if (!options.contains("--output") || options.contains("--size-mib") == options.contains("--size-bytes")) {
            throw std::invalid_argument("Specify --output and exactly one of --size-mib or --size-bytes");
        }
        // Windows file offsets are signed 64-bit. Refuse overflowing sizes before creating output.
        constexpr auto maximum = static_cast<std::uint64_t>(INT64_MAX);
        const auto bytes = options.contains("--size-mib") ?
            number(options.at("--size-mib"), maximum / (1024 * 1024), true) * (1024 * 1024) :
            number(options.at("--size-bytes"), maximum, true);
        const auto seed = number(option(options, "--seed", "42"), UINT64_MAX, true);
        const auto output = path(options.at("--output"));
        analyzer::generate_dataset(output, bytes, seed, options.contains("--force"));
        std::cout << "Generated " << bytes << " bytes at " << analyzer::path_text(output) << " (seed " << seed << ")\n";
        return 0;
    }
    if (command != "analyze" && command != "benchmark" && command != "__trial") throw std::invalid_argument("Unknown command: " + command);
    if (args.size() < 3 || args[2].starts_with("--")) throw std::invalid_argument("Specify an input file");
    const auto input = path(args[2]);
    if (command == "benchmark") {
        const auto options = parse_options(args, 3, {"--threads", "--runs", "--buffer-kib", "--output", "--order-seed"});
        analyzer::BenchmarkOptions configuration;
        configuration.input = input;
        configuration.threads = thread_list(option(options, "--threads", "1,2,4"));
        configuration.runs = static_cast<unsigned>(number(option(options, "--runs", "7"), 100));
        configuration.buffer_size = buffer_size(options);
        configuration.order_seed = number(option(options, "--order-seed", "42"), UINT64_MAX, true);
        configuration.csv_output = path(option(options, "--output", "results/benchmark.csv"));
        if (configuration.csv_output.extension() != ".csv") throw std::invalid_argument("Benchmark output must use a .csv extension");
        for (auto output : {configuration.csv_output, configuration.csv_output.parent_path() / (configuration.csv_output.stem().wstring() + L".summary.csv"),
                           configuration.csv_output.parent_path() / (configuration.csv_output.stem().wstring() + L".md"),
                           configuration.csv_output.parent_path() / (configuration.csv_output.stem().wstring() + L".metadata.txt")}) {
            if (analyzer::same_file(input, output)) throw std::invalid_argument("Report output would overwrite input");
        }
#ifndef NDEBUG
        std::cout << "Warning: debug build; use Release for performance comparisons.\n";
#endif
        std::cout << "Benchmarking " << analyzer::path_text(input) << "\n"
                  << "Same parser, shuffled rounds, fresh trial processes, pre-read cache.\n";
        const auto result = analyzer::run_benchmark(configuration);
        analyzer::write_reports(configuration, result);
        analyzer::print_comparison(result);
        std::cout << "\nReports saved alongside " << analyzer::path_text(configuration.csv_output) << '\n';
        return 0;
    }
    const auto allowed = command == "__trial" ?
        std::set<std::string>{"--mode", "--threads", "--buffer-kib", "--output"} :
        std::set<std::string>{"--mode", "--threads", "--buffer-kib"};
    const auto options = parse_options(args, 3, allowed);
    const auto mode = analyzer::parse_mode(option(options, "--mode", "mapped-parallel"));
    const auto count = workers(options, mode == analyzer::Mode::mapped_parallel ? analyzer::default_workers() : 1);
    if (mode != analyzer::Mode::mapped_parallel && count != 1) throw std::invalid_argument("This mode uses one worker; use --threads 1");
    if (command == "__trial") {
        if (!options.contains("--output")) throw std::invalid_argument("Missing trial output");
        const auto output = path(options.at("--output"));
        if (analyzer::same_file(input, output)) throw std::invalid_argument("Trial output would overwrite input");
        const auto measured = analyzer::measure(input, mode, count, buffer_size(options));
        analyzer::write_trial(output, measured);
        return 0;
    }
    const auto measured = analyzer::measure(input, mode, count, buffer_size(options));
    const auto& r = measured.analysis.result;
    std::cout << "\nFile: " << analyzer::path_text(input) << "\nMethod: " << analyzer::mode_name(mode)
              << "\nWorkers requested/used: " << count << '/' << measured.analysis.workers
              << "\nBytes: " << r.bytes << "\nLines: " << r.lines << "\nWords: " << r.words
              << "\nERROR: " << r.errors << "\nWARN: " << r.warnings << "\nINFO: " << r.infos
              << std::fixed << std::setprecision(6) << "\nElapsed: " << measured.metrics.seconds << " s"
              << "\nCPU time: " << measured.metrics.cpu_seconds << " s"
              << std::setprecision(2) << "\nThroughput: " << analyzer::throughput(measured) << " MiB/s"
              << "\nPeak working set: " << static_cast<double>(measured.metrics.peak_resident_bytes) / (1024 * 1024) << " MiB"
              << "\nPage faults (soft + hard): " << measured.metrics.page_faults << '\n';
    return 0;
}
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    try { return execute(arguments()); }
    catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\nUse --help for usage.\n";
        return 1;
    }
}
