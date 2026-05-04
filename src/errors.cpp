#include "errors.h"

#include <algorithm>
#include <format>
#include <iostream>

namespace acu {
namespace {
const std::string_view reset_code = "\033[0m";
const std::string_view blue_bold = "\033[1;34m";

void print_source_block(
    const Source& source,
    Location location,
    const std::string& label_msg,
    std::string_view label_color
) {
    auto location_str = location.to_string(source);
    std::cerr << std::format(
        "  {}-->{} {}\n", blue_bold, reset_code, location_str
    );

    // Calculate line number
    auto line_number = std::count(
                           source.content.begin(),
                           source.content.begin() + location.start,
                           '\n'
                       ) +
                       1;
    std::string line_num_str = std::to_string(line_number);
    std::size_t line_num_width = line_num_str.size();
    std::string padding(line_num_width, ' ');

    std::cerr << std::format("  {} {} |{}\n", padding, blue_bold, reset_code);

    // Snippet extraction
    auto start = (location.start > 0 && source.content[location.start] == '\n')
                     ? location.start - 1
                     : location.start;
    std::size_t line_start = source.content.rfind('\n', start);
    if (line_start == std::string::npos) {
        line_start = 0;
    } else {
        line_start += 1;
    }

    std::size_t line_end = source.content.find('\n', location.start);
    if (line_end == std::string::npos) {
        line_end = source.content.size();
    }

    std::string_view line_content =
        std::string_view(source.content)
            .substr(line_start, line_end - line_start);

    std::cerr << std::format(
        "  {} {} |{} {}\n", line_num_str, blue_bold, reset_code, line_content
    );

    // Caret
    auto column = static_cast<std::uint32_t>(start - line_start);
    std::string caret_line(column, ' ');
    caret_line += label_color;
    caret_line += "^";

    if (location.end > location.start) {
        std::uint32_t length = location.end - location.start;
        if (location.start + length > line_end) {
            length = static_cast<std::uint32_t>(line_end - location.start);
        }
        if (length > 1) {
            caret_line += std::string(length - 1, '~');
        }
    }

    if (!label_msg.empty()) {
        caret_line += " " + label_msg;
    }
    caret_line += reset_code;

    std::cerr << std::format(
        "  {} {} |{} {}\n", padding, blue_bold, reset_code, caret_line
    );
}

void print_diagnostic(
    std::string_view severity,
    const std::string& message,
    std::string_view color_code,
    const Source& main_source,
    Location main_location,
    const std::vector<Label>& labels,
    const std::string& hint
) {
    // Header
    std::cerr << std::format(
        "{}{}{}: {}\n", color_code, severity, reset_code, message
    );

    print_source_block(main_source, main_location, "", color_code);

    for (const auto& label : labels) {
        print_source_block(
            *label.source, label.location, label.message, "\033[1;36m"
        );  // Note color (Cyan)
    }

    if (!hint.empty()) {
        std::cerr << std::format(
            "  {}={} {}hint{}: {}\n",
            blue_bold,
            reset_code,
            blue_bold,
            reset_code,
            hint
        );
    }
    std::cerr << "\n";
}
}

void ErrorHandler::emit_all() const {
    for (const auto& err : errors_) {
        std::string_view severity_str;
        std::string_view color_code;

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

        print_diagnostic(
            severity_str,
            err.message,
            color_code,
            *err.source,
            err.location,
            err.labels,
            err.hint
        );
    }
}

}
