#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <filesystem>
#include <unordered_set>

namespace acu {
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

struct Source {
    std::string module_name;
    std::filesystem::path path;
    std::string content;
    Strings strings;
};

struct Location {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};
}
