#pragma once
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <windows.h>

namespace analyzer {
inline std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Text too long");
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Cannot convert text to UTF-8");
    std::string result(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            result.data(), size, nullptr, nullptr)) throw std::runtime_error("Cannot convert text to UTF-8");
    return result;
}
inline std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Text too long");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 text");
    std::wstring result(size, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                            result.data(), size)) throw std::runtime_error("Invalid UTF-8 text");
    return result;
}
inline std::string path_text(const std::filesystem::path& path) { return utf8(path.native()); }
inline bool same_file(const std::filesystem::path& first, const std::filesystem::path& second) {
    if (std::filesystem::exists(first) && std::filesystem::exists(second)) {
        return std::filesystem::equivalent(first, second);
    }
    return std::filesystem::absolute(first).lexically_normal() == std::filesystem::absolute(second).lexically_normal();
}
}
