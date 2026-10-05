#pragma once
#include <cstdint>

namespace analyzer {
struct AnalysisResult {
    std::uint64_t bytes{};
    std::uint64_t lines{};
    std::uint64_t words{};
    std::uint64_t errors{};
    std::uint64_t warnings{};
    std::uint64_t infos{};

    bool operator==(const AnalysisResult&) const = default;
    AnalysisResult& operator+=(const AnalysisResult& other) noexcept {
        bytes += other.bytes;
        lines += other.lines;
        words += other.words;
        errors += other.errors;
        warnings += other.warnings;
        infos += other.infos;
        return *this;
    }
};
}
