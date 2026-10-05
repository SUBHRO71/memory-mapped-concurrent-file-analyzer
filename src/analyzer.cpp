#include "analyzer/analyzer.hpp"
#include "analyzer/mapped_file.hpp"
#include "analyzer/scanner.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace analyzer {
std::string_view mode_name(Mode mode) noexcept {
    switch (mode) {
        case Mode::buffered: return "buffered";
        case Mode::mapped: return "mapped";
        case Mode::mapped_parallel: return "mapped-parallel";
    }
    return "unknown";
}

Mode parse_mode(std::string_view name) {
    if (name == "buffered") return Mode::buffered;
    if (name == "mapped") return Mode::mapped;
    if (name == "mapped-parallel") return Mode::mapped_parallel;
    throw std::invalid_argument("Mode must be buffered, mapped, or mapped-parallel");
}

unsigned default_workers() noexcept {
    return std::clamp(std::thread::hardware_concurrency(), 1u, max_workers);
}

std::vector<Range> partition_lines(std::span<const char> data, unsigned workers) {
    if (workers == 0 || workers > max_workers) throw std::invalid_argument("Workers must be in [1, 256]");
    std::vector<Range> ranges;
    if (data.empty()) return ranges;
    workers = static_cast<unsigned>(std::min<std::size_t>(workers, data.size()));
    ranges.reserve(workers);
    std::size_t start = 0;
    for (unsigned i = 1; i < workers; ++i) {
        // Avoid overflow in data.size() * i; reuse an already advanced boundary.
        std::size_t cut = (data.size() / workers) * i + ((data.size() % workers) * i) / workers;
        cut = std::max(cut, start);
        if (cut < data.size() && cut > 0 && data[cut - 1] != '\n') {
            while (cut < data.size() && data[cut] != '\n') ++cut;
            if (cut < data.size()) ++cut;
        }
        if (cut > start) ranges.push_back({start, cut});
        start = cut;
    }
    if (start < data.size()) ranges.push_back({start, data.size()});
    return ranges;
}

Output analyze(const std::filesystem::path& path, Mode mode, unsigned workers, std::size_t buffer_size) {
    if (workers == 0 || workers > max_workers) throw std::invalid_argument("Workers must be in [1, 256]");
    if (buffer_size == 0 || buffer_size > 64 * default_buffer_size) {
        throw std::invalid_argument("Buffer size must be in [1 byte, 64 MiB]");
    }
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Input must be an existing regular file");
    if (mode == Mode::buffered) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open input for buffered reading");
        std::vector<char> buffer(buffer_size);
        Scanner scanner;
        while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount() > 0) {
            scanner.consume({buffer.data(), static_cast<std::size_t>(input.gcount())});
        }
        if (!input.eof() || input.bad()) throw std::runtime_error("Buffered read failed");
        return {scanner.finish(), 1};
    }

    const MappedFile file(path);
    const auto data = file.bytes();
    if (mode == Mode::mapped) {
        Scanner scanner;
        scanner.consume(data);
        return {scanner.finish(), 1};
    }
    if (mode != Mode::mapped_parallel) throw std::invalid_argument("Invalid analysis mode");
    const auto ranges = partition_lines(data, workers);
    if (ranges.empty()) return {{}, 0};
    if (ranges.size() == 1) {
        Scanner scanner;
        scanner.consume(data);
        return {scanner.finish(), 1};
    }
    // All allocations precede workers; the scanning function cannot throw.
    std::vector<AnalysisResult> partial(ranges.size());
    std::vector<std::jthread> threads;
    threads.reserve(ranges.size());
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        threads.emplace_back([&, i] {
            Scanner scanner;
            scanner.consume(data.subspan(ranges[i].begin, ranges[i].end - ranges[i].begin));
            partial[i] = scanner.finish();
        });
    }
    for (auto& thread : threads) thread.join();
    AnalysisResult combined;
    for (const auto& result : partial) combined += result;
    return {combined, static_cast<unsigned>(ranges.size())};
}
}
