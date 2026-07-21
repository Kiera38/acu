#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "package_name.h"

namespace acu {
class Source;
class Strings {
public:
    std::string_view intern(std::string_view str) {
        if (auto it = strings.find(str); it != strings.end()) {
            return *it;
        }
        auto [it, inserted] = strings.insert(std::string(str));
        return *it;
    }

private:
    struct StringHash : std::hash<std::string_view> {
        using is_transparent = void;
    };

    struct StringEqual : std::equal_to<std::string_view> {
        using is_transparent = void;
    };

    std::unordered_set<std::string, StringHash, StringEqual> strings;
};

using Position = std::pair<std::uint32_t, std::uint32_t>;

struct Location {
    std::uint32_t start = 0;
    std::uint32_t end = 0;

    [[nodiscard]] Location merge(Location location) const {
        return {.start = start, .end = location.end};
    }
};

struct Line {
    std::uint32_t number;
    std::uint32_t offset;
    std::string_view text;
    [[nodiscard]] std::uint32_t length() const {return text.length();}
};

class Source {
public:
    Source(std::string module_name, std::filesystem::path file_path);
    Source(std::string module_name, std::filesystem::path file_path, std::string content);

    [[nodiscard]] std::string_view name() const { return name_; }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] std::string_view content() const { return content_; }
    [[nodiscard]] PackageNameRef module_name() const { return module_name_; }
    [[nodiscard]] Strings& strings() { return strings_; }

    [[nodiscard]] Position position(std::uint32_t byte) const;
    [[nodiscard]] Line line(std::uint32_t line) const;
    [[nodiscard]] std::uint32_t line_count() const { return lines_.size(); }

private:
    std::string name_;
    std::filesystem::path path_;
    std::string content_;
    Strings strings_;
    PackageName module_name_;
    std::vector<std::uint32_t> lines_;
};
}
