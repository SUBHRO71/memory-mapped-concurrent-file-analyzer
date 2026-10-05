#include "analyzer/metrics.hpp"
#include <chrono>
#include <system_error>
#include <windows.h>
#include <psapi.h>

namespace analyzer {
namespace {
struct Snapshot { std::uint64_t cpu_ticks{}; DWORD faults{}; SIZE_T peak{}; };
std::uint64_t ticks(FILETIME value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
Snapshot snapshot() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Read CPU metrics");
    }
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Read memory metrics");
    }
    return {ticks(kernel) + ticks(user), memory.PageFaultCount, memory.PeakWorkingSetSize};
}
}

Measurement measure(const std::filesystem::path& path, Mode mode, unsigned workers, std::size_t buffer_size) {
    const auto before = snapshot();
    const auto start = std::chrono::steady_clock::now();
    const auto analysis = analyze(path, mode, workers, buffer_size);
    const auto end = std::chrono::steady_clock::now();
    const auto after = snapshot();
    return {analysis, {std::chrono::duration<double>(end - start).count(),
                      static_cast<double>(after.cpu_ticks - before.cpu_ticks) / 10000000.0,
                      static_cast<std::uint64_t>(after.peak),
                      static_cast<DWORD>(after.faults - before.faults)}};
}

double throughput(const Measurement& measurement) noexcept {
    if (measurement.metrics.seconds <= 0) return 0;
    return (static_cast<double>(measurement.analysis.result.bytes) / (1024.0 * 1024.0)) /
           measurement.metrics.seconds;
}
}
