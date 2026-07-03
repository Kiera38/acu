#pragma once
#include <span>
#include <string_view>


namespace acu::unicode {
// Helper functions for UTF-8 handling (moved outside class)
inline bool is_utf8_start_byte(unsigned char c) {
    return (c & 0x80) == 0 || (c & 0xE0) == 0xC0 || (c & 0xF0) == 0xE0 ||
           (c & 0xF8) == 0xF0;
}

template <std::input_iterator Iter>
    requires std::same_as<std::iter_value_t<Iter>, char>
std::pair<char32_t, Iter> decode_utf8(Iter iter) { // ужас?
    auto byte1 = static_cast<unsigned char>(*iter);
    if ((byte1 & 0x80) == 0) {
        // ASCII
        return {static_cast<char32_t>(byte1), std::next(iter)};
    }
    if ((byte1 & 0xE0) == 0xC0) {
        iter = std::next(iter);
        auto byte2 = static_cast<unsigned char>(*iter);
        char32_t ch = ((byte1 & 0x1F) << 6) | (byte2 & 0x3F);
        return {ch, std::next(iter)};
    }
    if ((byte1 & 0xF0) == 0xE0) {
        iter = std::next(iter);
        auto byte2 = static_cast<unsigned char>(*iter);
        iter = std::next(iter);
        auto byte3 = static_cast<unsigned char>(*iter);
        char32_t ch =
            ((byte1 & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
        return {ch, std::next(iter)};
    }
    iter = std::next(iter);
    auto byte2 = static_cast<unsigned char>(*iter);
    iter = std::next(iter);
    auto byte3 = static_cast<unsigned char>(*iter);
    iter = std::next(iter);
    auto byte4 = static_cast<unsigned char>(*iter);
    char32_t ch = ((byte1 & 0x07) << 18) | ((byte2 & 0x3F) << 12) |
                  ((byte3 & 0x3F) << 6) | (byte4 & 0x3F);
    return {ch, std::next(iter)};
}

inline std::pair<char32_t, size_t> decode_utf8(
    std::string_view str, size_t pos
) {
    if (pos >= str.size()) {
        return {U'\0', 0};
    }

    auto byte1 = static_cast<unsigned char>(str[pos]);

    if ((byte1 & 0x80) == 0) {  // ASCII
        return {static_cast<char32_t>(byte1), 1};
    } else if ((byte1 & 0xE0) == 0xC0) {  // 2-byte sequence
        if (pos + 1 >= str.size()) return {U'\0', 0};
        auto byte2 = static_cast<unsigned char>(str[pos + 1]);
        char32_t ch = ((byte1 & 0x1F) << 6) | (byte2 & 0x3F);
        return {ch, 2};
    } else if ((byte1 & 0xF0) == 0xE0) {  // 3-byte sequence
        if (pos + 2 >= str.size()) return {U'\0', 0};
        auto byte2 = static_cast<unsigned char>(str[pos + 1]);
        auto byte3 = static_cast<unsigned char>(str[pos + 2]);
        char32_t ch =
            ((byte1 & 0x0F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
        return {ch, 3};
    } else if ((byte1 & 0xF8) == 0xF0) {  // 4-byte sequence
        if (pos + 3 >= str.size()) return {U'\0', 0};
        auto byte2 = static_cast<unsigned char>(str[pos + 1]);
        auto byte3 = static_cast<unsigned char>(str[pos + 2]);
        auto byte4 = static_cast<unsigned char>(str[pos + 3]);
        char32_t ch = ((byte1 & 0x07) << 18) | ((byte2 & 0x3F) << 12) |
                      ((byte3 & 0x3F) << 6) | (byte4 & 0x3F);
        return {ch, 4};
    }

    return {U'\0', 0};  // Invalid UTF-8
}

inline size_t encode_utf8(char32_t ch, std::span<char, 4> buffer) {
    if (ch < 0x80) {
        buffer[0] = static_cast<char>(ch);
        return 1;
    } else if (ch < 0x800) {
        buffer[0] = static_cast<char>(0xC0 | (ch >> 6));
        buffer[1] = static_cast<char>(0x80 | (ch & 0x3F));
        return 2;
    } else if (ch < 0x10000) {
        buffer[0] = static_cast<char>(0xE0 | (ch >> 12));
        buffer[1] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buffer[2] = static_cast<char>(0x80 | (ch & 0x3F));
        return 3;
    } else if (ch < 0x110000) {
        buffer[0] = static_cast<char>(0xF0 | (ch >> 18));
        buffer[1] = static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
        buffer[2] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buffer[3] = static_cast<char>(0x80 | (ch & 0x3F));
        return 4;
    }
    return 0;  // Invalid Unicode
}

// Helper function to check if a Unicode character is alphabetic
inline bool is_alpha(char32_t c) {
    // Basic Latin letters
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'_') {
        return true;
    }
    // Cyrillic letters (basic range)
    if ((c >= 0x0410 && c <= 0x042F) ||
        (c >= 0x0430 && c <= 0x044F)) {  // А-Я, а-я
        return true;
    }
    // Greek letters (basic range)
    if ((c >= 0x0391 && c <= 0x03A9) ||
        (c >= 0x03B1 && c <= 0x03C9)) {  // Α-Ω, α-ω
        return true;
    }
    // Common accented Latin letters
    if ((c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) ||
        (c >= 0xF8 && c <= 0xFF)) {
        return true;
    }
    // Extended Latin letters
    if ((c >= 0x100 && c <= 0x17F) || (c >= 0x180 && c <= 0x24F)) {
        return true;
    }
    // CJK letters (simplified)
    if ((c >= 0x4E00 && c <= 0x9FFF)) {
        return true;
    }
    // Arabic letters
    if ((c >= 0x0600 && c <= 0x06FF)) {
        return true;
    }
    // Hebrew letters
    if ((c >= 0x0590 && c <= 0x05FF)) {
        return true;
    }
    return false;
}

// Helper function to check if a Unicode character is alphanumeric
inline bool is_alnum(char32_t c) {
    return is_alpha(c) || (c >= U'0' && c <= U'9');
}

// Helper function to check if a Unicode character is a digit
inline bool is_digit(char32_t c) { return c >= U'0' && c <= U'9'; }

template <std::ranges::range Range>
    requires std::same_as<std::ranges::range_value_t<Range>, char>
class Chars: public std::ranges::view_interface<Chars<Range>> {
public:
    explicit Chars(Range range) : range(std::move(range)) {}

    class Iterator {
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = char32_t;
        Iterator() = default;
        explicit Iterator(std::ranges::const_iterator_t<Range> iterator)
            : iterator_(iterator) {}
        char32_t operator*() const {
            if (!value_) {
                value_ = decode_utf8(iterator_);
            }
            return value_->first;
        }
        Iterator& operator++() {
            if (!value_) {
                value_ = decode_utf8(iterator_);
            }
            iterator_ = value_->second;
            value_ = std::nullopt;
            return *this;
        }
        Iterator operator++(int) {
            Iterator it(*this);
            ++it;
            return it;
        }
        bool operator==(const Iterator& other) const {
            return iterator_ == other.iterator_;
        }
        bool operator!=(const Iterator& other) const {
            return iterator_ != other.iterator_;
        }


    private:
        std::ranges::const_iterator_t<Range> iterator_;
        std::optional<std::pair<char32_t, std::ranges::const_iterator_t<Range>>>
            value_;
    };

    [[nodiscard]] Iterator begin() const {
        return Iterator(std::ranges::cbegin(range));
    }
    [[nodiscard]] Iterator end() const {
        return Iterator(std::ranges::cend(range));
    }

private:
    Range range;
};

static_assert(std::input_iterator<Chars<std::string_view>::Iterator>);
static_assert(std::ranges::view<Chars<std::string_view>>);

}
