#include "errors.h"

#include <algorithm>
#include <cassert>
#include <format>
#include <iostream>
#include <map>

namespace acu {
namespace {
constexpr std::string_view reset_code = "\033[0m";
constexpr std::size_t tab_width = 4;
constexpr std::size_t label_context_lines = 1;
namespace colors {
constexpr std::string_view red = "\033[0;31m";
constexpr std::string_view yellow = "\033[0;33m";
constexpr std::string_view bold_yellow = "\033[1;33m";
constexpr std::string_view magenta = "\033[0;35m";
constexpr std::string_view bold_cyan = "\033[1;36m";
constexpr std::string_view bright_cyan = "\033[0;96m";
constexpr std::string_view cyan_underline_bold = "\033[1;4;37m";
constexpr std::string_view bold_green = "\033[1;33m";
}
namespace style {
constexpr std::string_view error = colors::red;
constexpr std::string_view warning = colors::yellow;
constexpr std::string_view note = colors::magenta;
constexpr std::string_view line_number = colors::bright_cyan;
constexpr std::string_view link = colors::cyan_underline_bold;
constexpr std::array<std::string_view, 3> highlights = {
    colors::bold_cyan, colors::bold_yellow, colors::bold_green
};
namespace chars {
constexpr std::string_view hbar = "─";
constexpr std::string_view vbar = "│";
constexpr std::string_view xbar = "┼";
constexpr std::string_view vbar_break = "·";
constexpr std::string_view uarrow = "▲";
constexpr std::string_view rarrow = "▶";
constexpr std::string_view ltop = "╭";
constexpr std::string_view mtop = "┬";
constexpr std::string_view rtop = "╮";
constexpr std::string_view lbot = "╰";
constexpr std::string_view mbot = "┴";
constexpr std::string_view rbot = "╯";
constexpr std::string_view lbox = "[";
constexpr std::string_view rbox = "]";
constexpr std::string_view lcross = "├";
constexpr std::string_view rcross = "┤";
constexpr std::string_view underbar = "┬";
constexpr std::string_view underline = "─";
constexpr std::string_view error = "error";
constexpr std::string_view warning = "warning";
constexpr std::string_view note = "note";
constexpr std::string_view fatal = "fatal";

}
}
struct LabelStyle {
    const Label* label;
    std::string_view style;

    [[nodiscard]] Location location() const { return label->location; }

    [[nodiscard]] std::uint32_t length() const {
        return location().end - location().start;
    }
};
enum class LabelRenderMode : std::uint8_t {
    SingleLine,
    MultiLineFirst,
    MultiLineRest
};
class SourceGroup {
public:
    explicit SourceGroup(const Source& source) : source_(&source) {}
    [[nodiscard]] const Source& source() const { return *source_; }

    void add_label(const Label& label) {
        assert(source_ == label.source);
        auto pos_start = source_->position(label.location.start);
        auto pos_end = source_->position(label.location.end);
        add_context(pos_start.first);
        add_context(pos_end.first);
        labels_.emplace_back(&label, style::highlights[current_style]);
        current_style++;
        if (current_style >= style::highlights.size()) {
            current_style = 0;
        }
    }
    void print(bool is_first) {
        std::ranges::sort(
            labels_, std::less<std::uint32_t> {}, [](const auto& label) {
                return label.location().start;
            }
        );
        std::size_t max_gutter = 0;

        for (const auto& line : lines_ | std::views::values) {
            std::size_t num_highlights = 0;
            for (const auto& label : labels_) {
                if (!span_line_only(line, label) &&
                    span_applies_gutter(line, label)) {
                    ++num_highlights;
                }
            }
            max_gutter = std::max(max_gutter, num_highlights);
        }
        std::size_t line_number_width =
            get_line_number_width(lines_.rbegin()->first);
        // header
        std::cerr << std::format(
            "{:{}}{}{}",
            "",
            line_number_width + 2,
            style::chars::ltop,
            style::chars::hbar
        );
        std::cerr << std::format(
            "[{}{}{}]\n", style::link, source_->name(), reset_code
        );

        for (const auto& line : lines_ | std::views::values) {
            write_line_number(line_number_width, line.number);
            render_line_gutter(max_gutter, line);
            render_line_text(line.text);
            std::vector<const LabelStyle*> single_line;
            std::vector<const LabelStyle*> multi_line;
            for (const auto& label : labels_) {
                if (span_applies(line, label)) {
                    if (span_line_only(line, label)) {
                        single_line.push_back(&label);
                    } else {
                        multi_line.push_back(&label);
                    }
                }
            }
            if (!single_line.empty()) {
                write_no_line_number(line_number_width);
                render_highlight_gutter(
                    max_gutter, line, LabelRenderMode::SingleLine
                );
                render_single_line_highlights(
                    line, line_number_width, max_gutter, single_line
                );
            }
            for (auto* label : multi_line) {
                if (span_ends(line, *label) && !span_starts(line, *label)) {
                    render_multi_line_end(
                        max_gutter, line_number_width, line, *label
                    );
                }
            }
        }
        auto hbar = std::views::repeat(style::chars::hbar, 4) |
                    std::views::join | std::ranges::to<std::string>();
        std::cerr << std::format(
            "{:{}}{}{}", "", line_number_width + 2, style::chars::lbot, hbar
        );
    }

private:
    void add_context(std::uint32_t position) {
        auto context_start = position <= label_context_lines
                                 ? 0
                                 : position - label_context_lines;
        auto context_end =
            source_->line_count() - label_context_lines <= position
                ? source_->line_count()
                : position + label_context_lines;
        for (std::uint32_t line = context_start; line <= context_end; ++line) {
            if (!lines_.contains(line)) {
                lines_[line] = source_->line(line);
            }
        }
    }

    void render_multi_line_end(
        std::size_t max_gutter,
        std::size_t line_number_width,
        const Line& line,
        const LabelStyle& label
    ) const {
        write_no_line_number(line_number_width);
        if (!label.label->message.empty()) {
            render_highlight_gutter(
                max_gutter, line, LabelRenderMode::SingleLine
            );
            std::cerr << std::format(
                "{}{} {}{}\n",
                label.style,
                style::chars::hbar,
                label.label->message,
                reset_code
            );
        } else {
            render_highlight_gutter(
                max_gutter, line, LabelRenderMode::SingleLine
            );
            std::cerr << std::format(
                "{}{}{}\n", label.style, style::chars::hbar, reset_code
            );
        }
    }

    void render_line_gutter(std::size_t max_gutter, const Line& line) const {
        if (max_gutter == 0) return;
        std::string gutter;
        auto applicable = labels_ | std::views::filter([&](const auto& label) {
                              return span_applies_gutter(line, label);
                          });
        bool arrow = false;
        for (const auto& label : applicable) {
            if (span_starts(line, label)) {
                gutter += label.style;
                gutter += style::chars::ltop;
                gutter.append_range(
                    std::views::repeat(
                        style::chars::hbar, max_gutter > 1 ? max_gutter - 1 : 0
                    ) |
                    std::views::join
                );
                gutter += style::chars::rarrow;
                gutter += reset_code;
                arrow = true;
                break;
            } else if (span_ends(line, label)) {
                gutter += label.style;
                if (label.label->message.empty()) {
                    gutter += style::chars::lbot;
                } else {
                    gutter += style::chars::lcross;
                }
                gutter.append_range(
                    std::views::repeat(
                        style::chars::hbar, max_gutter > 1 ? max_gutter - 1 : 0
                    ) |
                    std::views::join
                );
                gutter += style::chars::rarrow;
                gutter += reset_code;
                arrow = true;
                break;
            } else if (span_flyby(line, label)) {
                gutter += label.style;
                gutter += style::chars::vbar;
                gutter += reset_code;
            } else {
                gutter += ' ';
            }
        }
        std::cerr << std::format(
            "{}{:{}}",
            gutter,
            "",
            (arrow ? 1 : 3) + (gutter.length() > max_gutter
                                   ? 0
                                   : max_gutter - gutter.length())
        );
    }

    void render_highlight_gutter(
        std::size_t max_gutter, const Line& line, LabelRenderMode render_mode
    ) const {
        if (max_gutter == 0) return;

        std::size_t gutter_cols = 0;
        std::string gutter;
        auto applicable = labels_ | std::views::filter([&](const auto& label) {
                              return span_applies_gutter(line, label);
                          });
        for (const auto& [i, label] : applicable | std::views::enumerate) {
            if (!span_line_only(line, label) && span_ends(line, label)) {
                if (render_mode == LabelRenderMode::MultiLineRest) {
                    auto horizontal_space = max_gutter - i + 2;
                    for (std::size_t j = 0; j < horizontal_space; ++j) {
                        gutter += ' ';
                    }
                    gutter_cols += horizontal_space + 1;
                } else {
                    auto num_repeat = max_gutter - i + 2;
                    gutter += label.style;
                    gutter += style::chars::lbot;
                    gutter.append_range(
                        std::views::repeat(
                            style::chars::hbar,
                            num_repeat -
                                (render_mode == LabelRenderMode::MultiLineFirst
                                     ? 1
                                     : 0)
                        ) |
                        std::views::join
                    );
                    gutter += reset_code;
                    gutter_cols += num_repeat + 1;
                }
                break;
            } else {
                gutter += label.style;
                gutter += style::chars::vbar;
                gutter += reset_code;
                gutter_cols += 1;
            }
        }
        auto num_spaces = max_gutter + 3 - gutter_cols;
        std::cerr << std::format("{}{:{}}", gutter, "", num_spaces);
    }

    static void write_line_number(std::size_t width, std::size_t line_number) {
        std::cerr << std::format(
            " {}{:{}}{} {} ",
            style::line_number,
            line_number,
            width,
            reset_code,
            style::chars::vbar
        );
    }

    static void write_no_line_number(std::size_t width) {
        std::cerr << std::format(
            " {}{:{}}{} {} ",
            style::line_number,
            "",
            width,
            reset_code,
            style::chars::vbar
        );
    }

    static void line_visual_char_width(
        std::string_view text, std::invocable<char, std::size_t> auto func
    ) {
        std::size_t column = 0;
        bool escaped = false;
        for (auto c : text) {
            std::size_t width = 0;
            if (escaped) {
                if (c == 'm') escaped = false;
            } else {
                if (c == '\t') {
                    width = tab_width - column % tab_width;
                } else if (c == '\x1b') {
                    escaped = true;
                } else {
                    width = 1;
                }
            }
            column += width;
            std::invoke(func, c, width);
        }
    }

    static void render_line_text(std::string_view text) {
        line_visual_char_width(text, [&](char c, std::size_t width) {
            if (c == '\t') {
                for (std::size_t j = 0; j < width; ++j) {
                    std::cerr << ' ';
                }
            } else {
                std::cerr << c;
            }
        });
        // std::cerr << '\n';
    }

    static std::size_t visual_offset(
        const Line& line, std::size_t offset, bool start
    ) {
        auto text_index = offset - line.offset;
        std::string_view text =
            line.text.substr(0, std::min(text_index, line.text.length()));
        std::size_t text_width = 0;
        line_visual_char_width(text, [&](char c, std::size_t width) {
            text_width += width;
        });
        if (text_index > line.text.length()) {
            return text_width + 1;
        } else {
            return text_width;
        }
    }

    void render_single_line_highlights(
        const Line& line,
        size_t line_number_width,
        size_t max_gutter,
        std::span<const LabelStyle*> single_line
    ) const {
        std::string underlines;
        std::size_t highest = 0;
        std::vector<std::pair<const LabelStyle*, std::size_t>> vbar_offsets;
        for (auto label : single_line) {
            auto byte_start = label->label->location.start;
            auto byte_end = label->label->location.end;
            auto start =
                std::max(visual_offset(line, byte_start, true), highest);
            auto end =
                label->length() == 0
                    ? start + 1
                    : std::max(visual_offset(line, byte_end, false), start + 1);

            auto vbar_offset = (start + end) / 2;
            auto num_left = vbar_offset - start;
            auto num_right = end - vbar_offset - 1;
            underlines += std::format(
                "{}{:{}}{}{}{}{}",
                label->style,
                "",
                start > highest ? start - highest : 0,
                std::views::repeat(style::chars::underline, num_left) |
                    std::views::join | std::ranges::to<std::string>(),
                label->length() == 0            ? style::chars::uarrow
                : label->label->message.empty() ? style::chars::underline
                                                : style::chars::underbar,
                std::views::repeat(style::chars::underline, num_right) |
                    std::views::join | std::ranges::to<std::string>(),
                reset_code
            );
            highest = std::max(highest, end);
            vbar_offsets.emplace_back(label, vbar_offset);
        }
        std::cerr << underlines << '\n';

        for (auto label : single_line | std::views::reverse) {
            if (!label->label->message.empty()) {
                write_no_line_number(line_number_width);
                render_highlight_gutter(
                    max_gutter, line, LabelRenderMode::SingleLine
                );
                std::size_t curr_offset = 1;
                for (auto [offset_label, vbar_offset] : vbar_offsets) {
                    while (curr_offset < vbar_offset + 1) {
                        std::cerr << ' ';
                        curr_offset++;
                    }
                    if (offset_label != label) {
                        std::cerr << std::format(
                            "{}{}{}",
                            offset_label->style,
                            style::chars::vbar,
                            reset_code
                        );
                        curr_offset++;
                    } else {
                        std::cerr << std::format(
                            "{}{}{:2} {}{}\n",
                            label->style,
                            style::chars::lbot,
                            style::chars::hbar,
                            label->label->message,
                            reset_code
                        );
                        break;
                    }
                }
            }
        }
    }

    static std::size_t get_line_number_width(std::size_t number) {
        return static_cast<std::size_t>(std::log10(number)) + 1;
    }
    static bool span_line_only(const Line& line, const LabelStyle& label) {
        return label.location().start >= line.offset &&
               label.location().end <= line.offset + line.length();
    }
    static bool span_applies(const Line& line, const LabelStyle& label) {
        auto span_len = label.length() == 0 ? 1 : label.length();
        return (label.location().start >= line.offset &&
                label.location().start < line.offset + line.length()) ||
               (label.location().start < line.offset &&
                label.location().start + span_len >
                    line.offset + line.length()) ||
               (label.location().start + span_len > line.offset &&
                label.location().start + span_len <=
                    line.offset + line.length());
    }
    static bool span_applies_gutter(const Line& line, const LabelStyle& label) {
        auto span_len = label.length() == 0 ? 1 : label.length();
        return span_applies(line, label) &&
               !((label.location().start >= line.offset &&
                  label.location().start <= line.offset + line.length()) &&
                 (label.location().start + span_len > line.offset &&
                  label.location().start + span_len <=
                      line.offset + line.length()));
    }

    static bool span_flyby(const Line& line, const LabelStyle& label) {
        return label.location().start < line.offset &&
               label.location().end > line.offset + line.length();
    }

    static bool span_starts(const Line& line, const LabelStyle& label) {
        return label.location().start >= line.offset;
    }
    static bool span_ends(const Line& line, const LabelStyle& label) {
        return label.location().end >= line.offset &&
               label.location().end <= line.offset + line.length();
    }

    const Source* source_;
    std::map<std::uint32_t, Line> lines_;
    std::vector<LabelStyle> labels_;
    std::uint8_t current_style = 0;
};

void print_header(const Error& error) {}

void print_diagnostic(const Error& error) {
    print_header(error);
    std::vector<SourceGroup> source_groups;
    for (const auto& label : error.labels) {
        auto it = std::ranges::find(
            source_groups, label.source, [](const auto& group) {
                return &group.source();
            }
        );
        if (it == source_groups.end()) {
            source_groups.emplace_back(*label.source);
            source_groups.back().add_label(label);
        } else {
            it->add_label(label);
        }
    }
    bool is_first = true;
    for (auto& group : source_groups) {
        group.print(is_first);
    }
}

}

void ErrorHandler::emit_all() const {
    for (const auto& err : errors_) {
        print_diagnostic(err);
    }
}
}
