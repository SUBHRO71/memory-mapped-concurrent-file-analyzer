#pragma once
#include "analyzer/analysis_result.hpp"
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace analyzer {
inline constexpr std::size_t default_buffer_size = 1024 * 1024;
inline constexpr unsigned max_workers = 256;
enum class Mode { buffered, mapped, mapped_parallel };
std::string_view mode_name(Mode mode) noexcept;
Mode parse_mode(std::string_view mode);
unsigned default_workers() noexcept;

struct Range { std::size_t begin{}, end{}; };
std::vector<Range> partition_lines(std::span<const char> data, unsigned workers);

struct Output {
    AnalysisResult result;
    unsigned workers{};
};
Output analyze(const std::filesystem::path& path, Mode mode, unsigned workers = 1,
               std::size_t buffer_size = default_buffer_size);
}
