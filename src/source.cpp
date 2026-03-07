#include "source.h"

#include <string_view>

namespace acu {
std::string Location::to_string(const Source& source) const {
    auto line_start = source.content.rfind('\n', start);
    auto line_end = source.content.find('\n', end);
    if (line_start == std::string_view::npos) {
        line_start = 0;
    } else {
        line_start += 1;  // Move past the newline
    }
    if (line_end == std::string_view::npos) {
        line_end = source.content.size();
    }
    auto line = source.content.substr(line_start, line_end - line_start);
    auto line_number =
        std::count(
            source.content.begin(), source.content.begin() + start, '\n'
        ) +
        1;
    return std::format(
        "{}:{}:{}", source.path.string(), line_number, start - line_start
    );
}
}