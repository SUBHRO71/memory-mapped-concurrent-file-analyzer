#pragma once
#include "analyzer/analysis_result.hpp"
#include <array>
#include <cstddef>
#include <span>

namespace analyzer {
// Streaming state preserves tokens across buffered reads without allocating words.
class Scanner {
public:
    void consume(std::span<const char> bytes) noexcept;
    AnalysisResult finish() noexcept;
private:
    void finish_word() noexcept;
    AnalysisResult result_{};
    std::array<char, 5> token_{};
    std::size_t token_length_{}; // Saturates at 6: longer than every keyword.
    bool last_was_newline_{true};
    bool finished_{};
};
}
