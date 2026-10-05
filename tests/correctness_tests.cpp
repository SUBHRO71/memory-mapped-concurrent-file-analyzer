#include "analyzer/analyzer.hpp"
#include "analyzer/dataset_generator.hpp"
#include "analyzer/mapped_file.hpp"
#include "analyzer/scanner.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace {
unsigned checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void require_throw(F action, const std::string& message) {
    bool thrown = false;
    try { action(); } catch (const std::exception&) { thrown = true; }
    require(thrown, message);
}

analyzer::AnalysisResult reference(const std::string& text) {
    // Independent string searches, no production scanner or partitioner.
    analyzer::AnalysisResult result;
    result.bytes = text.size();
    result.lines = static_cast<std::uint64_t>(std::count(text.begin(), text.end(), '\n'));
    if (!text.empty() && text.back() != '\n') ++result.lines;
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
    auto start = text.find_first_of(alphabet);
    while (start != std::string::npos) {
        const auto end = text.find_first_not_of(alphabet, start);
        const auto token = text.substr(start, end - start);
        ++result.words;
        result.errors += token == "ERROR";
        result.warnings += token == "WARN";
        result.infos += token == "INFO";
        if (end == std::string::npos) break;
        start = text.find_first_of(alphabet, end);
    }
    return result;
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::ofstream stream(file, std::ios::binary);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream) throw std::runtime_error("Cannot write test fixture");
}
std::string read(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void verify(const std::filesystem::path& file, const std::string& text, const std::string& label, bool large = false) {
    write(file, text);
    const auto expected = reference(text);
    for (const auto buffer : (large ? std::vector<std::size_t>{1024, analyzer::default_buffer_size} : std::vector<std::size_t>{1, 7, 1024})) {
        require(analyzer::analyze(file, analyzer::Mode::buffered, 1, buffer).result == expected, label + ": buffered mismatch");
    }
    require(analyzer::analyze(file, analyzer::Mode::mapped).result == expected, label + ": mapped mismatch");
    for (const auto count : {1u, 2u, 4u, 32u}) {
        const auto result = analyzer::analyze(file, analyzer::Mode::mapped_parallel, count);
        require(result.result == expected, label + ": parallel mismatch");
        const auto ranges = analyzer::partition_lines(std::span(text.data(), text.size()), count);
        std::size_t covered = 0;
        for (const auto range : ranges) {
            require(range.begin == covered && range.end > range.begin && range.end <= text.size(), label + ": gap/overlap");
            require(range.begin == 0 || text[range.begin - 1] == '\n', label + ": partial start line");
            require(range.end == text.size() || text[range.end - 1] == '\n', label + ": partial end line");
            covered = range.end;
        }
        require(covered == text.size() && ranges.size() <= count, label + ": invalid coverage");
        require(result.workers == ranges.size(), label + ": incorrect effective worker count");
    }
}

struct TestDirectory {
    std::filesystem::path path;
    TestDirectory() {
        path = std::filesystem::temp_directory_path() / ("analyzer-tests-" + std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(path)) throw std::runtime_error("Cannot create test directory");
    }
    ~TestDirectory() {
        std::error_code error;
        for (const auto& name : {L"sample \u03b4 test.txt", L"one.log", L"two.log", L"empty.log"}) std::filesystem::remove(path / name, error);
        std::filesystem::remove(path, error);
    }
};
}

int main() {
    try {
        TestDirectory temporary;
        const auto file = temporary.path / L"sample \u03b4 test.txt";
        require(reference("") == analyzer::AnalysisResult{}, "Empty reference");
        require(reference("ERROR WARN INFO\n") == analyzer::AnalysisResult{16, 1, 3, 1, 1, 1}, "Known reference fixture");
        const std::vector<std::string> fixtures{
            "", "x", "\n", "\n\n", " \t\r", "ERROR WARN INFO\n", "ERROR\r\nWARN\r\nINFO",
            "ERRORS error WARN_WARNING INFORMATION _ERROR 123 INFO.", "ERROR!WARN?INFO\n\nERROR",
            std::string("ERROR\0WARN\nINFO", 15), "ERR\nOR WARN\nINFO", std::string("INFO\xc3\xa9" "ERROR", 11),
            "a\r\nb\r\nc\r\nd\r\n", "ERROR", "WARN\nINFO\nERROR\n"
        };
        for (std::size_t i = 0; i < fixtures.size(); ++i) verify(file, fixtures[i], "fixture " + std::to_string(i));
        std::mt19937 random(42);
        const std::string alphabet = "ABCDEabc012_ \n\r\t!?";
        for (unsigned i = 0; i < 60; ++i) {
            std::string text;
            const auto length = random() % 1500;
            for (unsigned j = 0; j < length; ++j) {
                if (random() % 50 == 0) text += "ERROR WARN INFO ";
                else text += alphabet[random() % alphabet.size()];
            }
            verify(file, text, "random " + std::to_string(i));
        }
        verify(file, std::string(analyzer::default_buffer_size - 2, ' ') + "ERROR INFO\nWARN", "1 MiB token boundary", true);
        verify(file, std::string(2 * analyzer::default_buffer_size + 17, 'a') + " ERROR", "oversized line and token", true);
        require(analyzer::analyze(file, analyzer::Mode::mapped_parallel, 32).workers == 1, "Long line must use one range");
        analyzer::Scanner scanner;
        scanner.consume(std::span("ERROR", 5));
        require(scanner.finish() == scanner.finish(), "Finish is idempotent");
        require_throw([&] { analyzer::analyze(file, analyzer::Mode::mapped_parallel, 0); }, "Reject zero workers");
        require_throw([&] { analyzer::partition_lines(std::span("x", 1), 257); }, "Reject excessive workers");
        require_throw([&] { analyzer::analyze(file, analyzer::Mode::buffered, 1, 0); }, "Reject zero buffer");
        require_throw([&] { analyzer::analyze(temporary.path / "missing", analyzer::Mode::mapped); }, "Reject missing file");
        require_throw([&] { analyzer::analyze(temporary.path, analyzer::Mode::buffered); }, "Reject directory");
        require_throw([] { analyzer::parse_mode("unknown"); }, "Reject unknown mode");
        const auto exclusive = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(exclusive != INVALID_HANDLE_VALUE, "Open exclusive fixture");
        bool mapping_rejected = false;
        try { analyzer::MappedFile locked(file); } catch (const std::exception&) { mapping_rejected = true; }
        CloseHandle(exclusive);
        require(mapping_rejected, "Mapping rejects incompatible writer");

        const auto first = temporary.path / "one.log";
        const auto second = temporary.path / "two.log";
        analyzer::generate_dataset(first, 65537, 42);
        analyzer::generate_dataset(second, 65537, 42);
        require(std::filesystem::file_size(first) == 65537, "Generator exact size");
        require(read(first) == read(second), "Generator reproducibility");
        require_throw([&] { analyzer::generate_dataset(first, 1, 0); }, "Generator protects existing files");
        analyzer::generate_dataset(temporary.path / "empty.log", 0, 0);
        require(std::filesystem::file_size(temporary.path / "empty.log") == 0, "Generator handles empty file");
        const auto expected = reference(read(first));
        for (const auto mode : {analyzer::Mode::buffered, analyzer::Mode::mapped, analyzer::Mode::mapped_parallel}) {
            require(analyzer::analyze(first, mode, 4).result == expected, "Generated dataset matches independent reference");
        }
        std::cout << "Passed " << checks << " correctness checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
