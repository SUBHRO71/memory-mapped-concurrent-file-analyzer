#include "analyzer/benchmark.hpp"
#include "analyzer/text.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <windows.h>

namespace analyzer {
namespace {
class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
private:
    HANDLE value_;
};

class TrialDirectory {
public:
    TrialDirectory() {
        const auto base = std::filesystem::temp_directory_path();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            directory_ = base / ("file-analyzer-" + std::to_string(GetCurrentProcessId()) + "-" +
                                 std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(directory_)) return;
        }
        throw std::runtime_error("Cannot allocate temporary trial directory");
    }
    std::filesystem::path next() {
        const auto path = directory_ / (std::to_string(files_.size()) + ".trial");
        files_.push_back(path);
        return path;
    }
    ~TrialDirectory() {
        std::error_code error;
        for (const auto& file : files_) std::filesystem::remove(file, error);
        std::filesystem::remove(directory_, error);
    }
private:
    std::filesystem::path directory_;
    std::vector<std::filesystem::path> files_;
};

std::filesystem::path executable_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) throw std::runtime_error("Cannot locate executable");
    return std::wstring(buffer.data(), size);
}

// Windows argv quoting; no shell interprets input paths or options.
std::wstring quote(std::wstring_view argument) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') {
            result.append(slashes * 2 + 1, L'\\');
        } else {
            result.append(slashes, L'\\');
        }
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}

void run_child(const std::filesystem::path& input, Configuration config, std::size_t buffer_size,
               const std::filesystem::path& output) {
    const auto executable = executable_path();
    const std::vector<std::wstring> arguments{
        executable.native(), L"__trial", input.native(), L"--mode", wide(mode_name(config.mode)),
        L"--threads", std::to_wstring(config.threads), L"--buffer-kib", std::to_wstring(buffer_size / 1024),
        L"--output", output.native()
    };
    std::wstring command;
    for (const auto& argument : arguments) {
        if (!command.empty()) command += L' ';
        command += quote(argument);
    }
    if (command.size() >= 32767) throw std::runtime_error("Trial command exceeds Windows command-line limit");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Launch benchmark trial");
    }
    const Handle process_handle(process.hProcess);
    const Handle thread_handle(process.hThread);
    if (WaitForSingleObject(process_handle.get(), INFINITE) != WAIT_OBJECT_0) {
        throw std::runtime_error("Failed to wait for benchmark trial");
    }
    DWORD exit_code{};
    if (!GetExitCodeProcess(process_handle.get(), &exit_code) || exit_code != 0) {
        throw std::runtime_error("Benchmark trial failed (exit " + std::to_string(exit_code) +
                                 "); run analyze with the same mode to see its error");
    }
}

Measurement read_trial(const std::filesystem::path& path) {
    std::ifstream input(path);
    Measurement measurement;
    auto& r = measurement.analysis.result;
    auto& m = measurement.metrics;
    if (!(input >> r.bytes >> r.lines >> r.words >> r.errors >> r.warnings >> r.infos >>
          measurement.analysis.workers >> m.seconds >> m.cpu_seconds >> m.peak_resident_bytes >> m.page_faults)) {
        throw std::runtime_error("Invalid or incomplete benchmark trial output");
    }
    input >> std::ws;
    if (!input.eof() || !std::isfinite(m.seconds) || m.seconds <= 0 ||
        !std::isfinite(m.cpu_seconds) || m.cpu_seconds < 0) throw std::runtime_error("Invalid trial metrics");
    return measurement;
}

void pre_read(const std::filesystem::path& path, std::size_t buffer_size) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open input for cache pre-read");
    std::vector<char> buffer(buffer_size);
    while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount() > 0) {}
    if (!input.eof() || input.bad()) throw std::runtime_error("Cache pre-read failed");
}

std::string registry_string(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    DWORD bytes{};
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS) return "unavailable";
    std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) return "unavailable";
    return utf8(value.data());
}

std::string compiler_description() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown compiler";
#endif
}

std::vector<std::pair<std::string, std::string>> metadata(const BenchmarkOptions& options) {
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    const bool memory_ok = GlobalMemoryStatusEx(&memory);
    wchar_t root[MAX_PATH]{};
    wchar_t filesystem[MAX_PATH]{};
    const auto absolute = std::filesystem::absolute(options.input);
    if (GetVolumePathNameW(absolute.c_str(), root, MAX_PATH)) {
        GetVolumeInformationW(root, nullptr, 0, nullptr, nullptr, nullptr, filesystem, MAX_PATH);
    }
    SYSTEMTIME now{};
    GetSystemTime(&now);
    std::ostringstream timestamp;
    timestamp << std::setfill('0') << std::setw(4) << now.wYear << '-' << std::setw(2) << now.wMonth
              << '-' << std::setw(2) << now.wDay << 'T' << std::setw(2) << now.wHour << ':'
              << std::setw(2) << now.wMinute << ':' << std::setw(2) << now.wSecond << 'Z';
    const auto size = std::filesystem::file_size(options.input);
    return {
        {"timestamp_utc", timestamp.str()},
        {"input", path_text(absolute)},
        {"file_bytes", std::to_string(size)},
        {"file_mtime_ticks", std::to_string(std::filesystem::last_write_time(options.input).time_since_epoch().count())},
        {"cpu", registry_string(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString")},
        {"logical_processors", std::to_string(default_workers())},
        {"physical_ram_bytes", memory_ok ? std::to_string(memory.ullTotalPhys) : "unavailable"},
        {"available_ram_bytes_at_start", memory_ok ? std::to_string(memory.ullAvailPhys) : "unavailable"},
        {"os", "Windows build " + registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber")},
        {"compiler", compiler_description()},
#ifdef NDEBUG
        {"build", "Release (NDEBUG)"},
#else
        {"build", "Debug (NDEBUG absent; do not use for performance conclusions)"},
#endif
        {"buffer_bytes", std::to_string(options.buffer_size)},
        {"runs_per_configuration", std::to_string(options.runs)},
        {"order_seed", std::to_string(options.order_seed)},
        {"cache_policy", memory_ok && size > memory.ullAvailPhys ?
            "pre-read before each trial; memory pressure; residency uncontrolled" :
            "pre-read before each trial; residency not guaranteed; no cold-cache claim"},
        {"volume_root", utf8(root)}, {"filesystem", utf8(filesystem)},
        {"storage_device", "Record SSD/HDD/model separately; not automatically detected"},
        {"memory_metric", "Lifetime peak working set of a fresh process; includes runtime baseline and resident mapped pages"},
        {"page_fault_metric", "Windows total process page-fault delta (soft and hard combined)"},
        {"timer_scope", "Input validation/open through close/unmap and worker join; excludes process startup, pre-read, metrics queries, output"}
    };
}
}

double quantile(std::vector<double> values, double fraction) {
    if (values.empty() || fraction < 0 || fraction > 1 || !std::isfinite(fraction)) {
        throw std::invalid_argument("Quantile requires observations and a fraction in [0, 1]");
    }
    std::sort(values.begin(), values.end());
    const double index = fraction * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(index);
    const auto upper = std::min(lower + 1, values.size() - 1);
    return values[lower] + (values[upper] - values[lower]) * (index - static_cast<double>(lower));
}

void write_trial(const std::filesystem::path& path, const Measurement& measurement) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) throw std::runtime_error("Cannot create trial output");
    const auto& r = measurement.analysis.result;
    const auto& m = measurement.metrics;
    output << std::setprecision(17) << r.bytes << ' ' << r.lines << ' ' << r.words << ' '
           << r.errors << ' ' << r.warnings << ' ' << r.infos << ' ' << measurement.analysis.workers << ' '
           << m.seconds << ' ' << m.cpu_seconds << ' ' << m.peak_resident_bytes << ' ' << m.page_faults << '\n';
    output.close();
    if (!output) throw std::runtime_error("Trial output write failed");
}

BenchmarkResult run_benchmark(const BenchmarkOptions& options) {
    if (options.runs < 1 || options.runs > 100) throw std::invalid_argument("Runs must be in [1, 100]");
    if (options.threads.empty() || options.buffer_size < 1024 || options.buffer_size > 64 * default_buffer_size ||
        options.buffer_size % 1024 != 0) throw std::invalid_argument("Invalid benchmark configuration");
    std::vector<Configuration> configurations{{Mode::buffered, 1}, {Mode::mapped, 1}};
    for (const auto count : options.threads) {
        if (count == 0 || count > max_workers) throw std::invalid_argument("Threads must be in [1, 256]");
        if (std::none_of(configurations.begin(), configurations.end(), [count](const auto& config) {
            return config.mode == Mode::mapped_parallel && config.threads == count;
        })) configurations.push_back({Mode::mapped_parallel, count});
    }
    const auto expected_size = std::filesystem::file_size(options.input);
    const auto expected_time = std::filesystem::last_write_time(options.input);
    const auto check_input = [&] {
        if (std::filesystem::file_size(options.input) != expected_size ||
            std::filesystem::last_write_time(options.input) != expected_time) {
            throw std::runtime_error("Input changed during the benchmark; comparison invalidated");
        }
    };
    BenchmarkResult result;
    result.metadata = metadata(options);
    result.reference = analyze(options.input, Mode::buffered, 1, options.buffer_size).result;
    check_input();
    TrialDirectory temporary;
    std::mt19937_64 random(options.order_seed);
    const auto total = configurations.size() * options.runs;
    for (unsigned round = 1; round <= options.runs; ++round) {
        auto order = configurations;
        std::shuffle(order.begin(), order.end(), random);
        for (const auto config : order) {
            check_input();
            std::cout << "Trial " << (result.trials.size() + 1) << '/' << total << ": "
                      << mode_name(config.mode) << " (requested workers " << config.threads << ")\n" << std::flush;
            pre_read(options.input, options.buffer_size);
            const auto output = temporary.next();
            run_child(std::filesystem::absolute(options.input), config, options.buffer_size, output);
            const auto measured = read_trial(output);
            check_input();
            if (measured.analysis.result != result.reference) {
                throw std::runtime_error("Analysis results differ: benchmark invalidated");
            }
            result.trials.push_back({config, round, measured});
        }
    }
    for (const auto config : configurations) {
        std::vector<double> seconds, cpu, memory, faults;
        unsigned workers = 0;
        for (const auto& trial : result.trials) {
            if (trial.configuration.mode != config.mode || trial.configuration.threads != config.threads) continue;
            seconds.push_back(trial.measurement.metrics.seconds);
            cpu.push_back(trial.measurement.metrics.cpu_seconds);
            memory.push_back(static_cast<double>(trial.measurement.metrics.peak_resident_bytes) / (1024 * 1024));
            faults.push_back(static_cast<double>(trial.measurement.metrics.page_faults));
            workers = trial.measurement.analysis.workers;
        }
        const double median = quantile(seconds, 0.5);
        result.summaries.push_back({config, workers, median, quantile(seconds, 0.75) - quantile(seconds, 0.25),
            (static_cast<double>(result.reference.bytes) / (1024 * 1024)) / median, 0, 0,
            quantile(cpu, 0.5), quantile(memory, 0.5), quantile(faults, 0.5)});
    }
    const double baseline = result.summaries[0].median_seconds;
    const double mapped = result.summaries[1].median_seconds;
    for (auto& summary : result.summaries) {
        summary.speedup = baseline / summary.median_seconds;
        summary.mapped_scaling = summary.configuration.mode == Mode::buffered ? 0 : mapped / summary.median_seconds;
    }
    return result;
}
}
