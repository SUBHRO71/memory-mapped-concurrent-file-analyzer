#include "analyzer/scanner.hpp"
#include <string_view>

namespace analyzer {
namespace {
bool is_word_byte(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}
}

void Scanner::finish_word() noexcept {
    if (token_length_ == 0) return;
    ++result_.words;
    if (token_length_ <= token_.size()) {
        const std::string_view token(token_.data(), token_length_);
        result_.errors += token == "ERROR";
        result_.warnings += token == "WARN";
        result_.infos += token == "INFO";
    }
    token_length_ = 0;
}

void Scanner::consume(std::span<const char> bytes) noexcept {
    if (finished_) return;
    for (const unsigned char c : bytes) {
        if (is_word_byte(c)) {
            if (token_length_ < token_.size()) token_[token_length_] = static_cast<char>(c);
            if (token_length_ <= token_.size()) ++token_length_;
        } else {
            finish_word();
        }
        result_.lines += c == '\n';
    }
    result_.bytes += bytes.size();
    if (!bytes.empty()) last_was_newline_ = bytes.back() == '\n';
}

AnalysisResult Scanner::finish() noexcept {
    if (!finished_) {
        finish_word();
        if (result_.bytes != 0 && !last_was_newline_) ++result_.lines;
        finished_ = true;
    }
    return result_;
}
}
