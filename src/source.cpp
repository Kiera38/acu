#include "source.h"

#include <cassert>
#include <fstream>
#include <string_view>

namespace acu {
Source::Source(std::string module_name, std::filesystem::path file_path)
    : name_(std::move(module_name)),
      path_(std::move(file_path)),
      module_name_(name_) {
    std::ifstream ifs(path_.string());
    ifs >> content_;
    lines_ = std::views::split(content_, '\n') |
             std::views::transform([&](auto c) {
                 return c.begin() - content_.begin();
             }) |
             std::ranges::to<std::vector<std::uint32_t>>();
}

Source::Source(
    std::string module_name,
    std::filesystem::path file_path,
    std::string content
)
    : name_(std::move(module_name)),
      path_(std::move(file_path)),
      content_(std::move(content)),
      module_name_(name_) {

    lines_ = std::views::split(content_, '\n') |
             std::views::transform([&](auto c) {
                 return c.begin() - content_.begin();
             }) |
             std::ranges::to<std::vector<std::uint32_t>>();
}

Position Source::position(std::uint32_t byte) const {
    auto it = std::ranges::lower_bound(lines_, byte);
    assert(it != lines_.end());
    auto line = it - lines_.begin();
    auto column = byte - *it;
    return {line, column};
}

Line Source::line(std::uint32_t line) const {
    auto start = lines_.at(line);
    if (line + 1 < lines_.size()) {
        auto end = lines_.at(line + 1);
        return {
            line, start, std::string_view(content_).substr(start, end - start)
        };
    }
    return {line, start, std::string_view(content_).substr(start)};
}
}