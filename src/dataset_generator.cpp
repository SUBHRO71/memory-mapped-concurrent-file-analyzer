#include "analyzer/dataset_generator.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace analyzer {
void generate_dataset(const std::filesystem::path& path, std::uint64_t bytes, std::uint64_t seed, bool overwrite) {
    if (std::filesystem::exists(path) && !overwrite) {
        throw std::runtime_error("Output already exists; choose another path or pass --force");
    }
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Cannot create dataset");
    std::uint64_t state = seed ^ 0x9e3779b97f4a7c15ULL;
    if (state == 0) state = 1;
    const auto random = [&state]() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 2685821657736338717ULL;
    };
    constexpr std::array<std::string_view, 6> levels{"INFO", "INFO", "INFO", "WARN", "ERROR", "DEBUG"};
    std::string batch;
    batch.reserve(1024 * 1024 + 1024);
    std::uint64_t remaining = bytes;
    while (remaining > 0) {
        const auto value = random();
        std::string line = "2026-01-01T12:00:00Z ";
        line += levels[value % levels.size()];
        line += " request_id=" + std::to_string(value % 1000000) + " service=file_analyzer elapsed_ms=";
        line += std::to_string((value >> 16) % 5000);
        line += " message=processed ";
        const auto repetitions = 1 + (value >> 32) % 12;
        for (std::uint64_t i = 0; i < repetitions; ++i) line += "record bytes token ";
        line += '\n';
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, line.size()));
        batch.append(line.data(), count);
        remaining -= count;
        if (batch.size() >= 1024 * 1024 || remaining == 0) {
            output.write(batch.data(), static_cast<std::streamsize>(batch.size()));
            if (!output) throw std::runtime_error("Dataset write failed (check free disk space)");
            batch.clear();
        }
    }
    output.close();
    if (!output) throw std::runtime_error("Dataset could not be finalized");
}
}
