#pragma once

#include <string>
#include <filesystem>

namespace acu {
struct Source {
    std::string module_name;
    std::filesystem::path path;
    std::string content;
};

struct Location {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};
}