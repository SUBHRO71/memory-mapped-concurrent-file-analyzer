#include "analyzer/mapped_file.hpp"
#include <limits>
#include <stdexcept>
#include <system_error>

namespace analyzer {
namespace {
[[noreturn]] void fail(const char* action) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), action);
}
}

MappedFile::MappedFile(const std::filesystem::path& path) {
    static_assert(sizeof(void*) == 8, "64-bit build required");
    try {
        file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) fail("Open file for mapping");
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file_, &size)) fail("Read file size");
        if (size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) >
            std::numeric_limits<std::size_t>::max()) {
            throw std::runtime_error("File is too large for this process's address space");
        }
        size_ = static_cast<std::size_t>(size.QuadPart);
        if (size_ == 0) return;
        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping_) fail("Create read-only mapping");
        data_ = static_cast<const char*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0));
        if (!data_) fail("Map file view");
    } catch (...) {
        close();
        throw;
    }
}

void MappedFile::close() noexcept {
    if (data_) UnmapViewOfFile(data_);
    if (mapping_) CloseHandle(mapping_);
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    data_ = nullptr;
    mapping_ = nullptr;
    file_ = INVALID_HANDLE_VALUE;
}

MappedFile::~MappedFile() { close(); }
}
