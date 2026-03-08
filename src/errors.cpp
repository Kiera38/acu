#include "errors.h"

#include <format>
#include <iostream>

namespace acu {

void ErrorHandler::emit_all(const Source& source) const {
    for (const auto& err : errors_) {
        std::string severity_str;
        std::string color_code;
        std::string reset_code = "\033[0m";

        switch (err.severity) {
            case Severity::Note:
                severity_str = "note";
                color_code = "\033[1;36m";  // Cyan
                break;
            case Severity::Warning:
                severity_str = "warning";
                color_code = "\033[1;33m";  // Yellow
                break;
            case Severity::Error:
                severity_str = "error";
                color_code = "\033[1;31m";  // Red
                break;
            case Severity::Fatal:
                severity_str = "fatal error";
                color_code = "\033[1;31m";  // Red
                break;
        }

        auto location_str = err.location.to_string(source);
        std::cerr << std::format(
            "{}: {}{}{}: {}\n",
            location_str,
            color_code,
            severity_str,
            reset_code,
            err.message
        );

        // Snippet extraction
        std::size_t line_start = source.content.rfind('\n', err.location.start);
        if (line_start == std::string::npos) {
            line_start = 0;
        } else {
            line_start += 1;
        }

        std::size_t line_end = source.content.find('\n', err.location.start);
        if (line_end == std::string::npos) {
            line_end = source.content.size();
        }

        std::string_view line = std::string_view(source.content)
                                    .substr(line_start, line_end - line_start);
        std::cerr << " " << line << "\n";

        // Caret
        std::uint32_t column =
            err.location.start - static_cast<std::uint32_t>(line_start);
        std::string caret(column, ' ');
        caret += color_code;
        caret += "^";
        caret += reset_code;

        if (err.location.end > err.location.start) {
            std::uint32_t length = err.location.end - err.location.start;
            // Cap length to the end of the line
            if (err.location.start + length > line_end) {
                length =
                    static_cast<std::uint32_t>(line_end - err.location.start);
            }
            if (length > 1) {
                caret += color_code;
                caret += std::string(length - 1, '~');
                caret += reset_code;
            }
        }
        std::cerr << " " << caret << "\n";

        if (!err.hint.empty()) {
            std::cerr << std::format(" \033[1;34mhint\033[0m: {}\n", err.hint);
        }
    }
}

}
