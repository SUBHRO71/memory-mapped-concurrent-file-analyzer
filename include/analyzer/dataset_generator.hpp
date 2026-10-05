#pragma once
#include <cstdint>
#include <filesystem>

namespace analyzer {
void generate_dataset(const std::filesystem::path& path, std::uint64_t bytes,
                      std::uint64_t seed, bool overwrite = false);
}
