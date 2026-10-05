#pragma once
#include <filesystem>
#include <span>
#include <windows.h>

namespace analyzer {
class MappedFile {
public:
    explicit MappedFile(const std::filesystem::path& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    std::span<const char> bytes() const noexcept { return {data_, size_}; }
private:
    void close() noexcept;
    HANDLE file_{INVALID_HANDLE_VALUE};
    HANDLE mapping_{};
    const char* data_{};
    std::size_t size_{};
};
}
