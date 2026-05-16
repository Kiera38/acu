#pragma once

#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hash.h"

namespace acu {
struct PackageNameRef : std::span<const std::string_view> {
    PackageNameRef() = default;
    PackageNameRef(std::span<const std::string_view> n)
        : std::span<const std::string_view>(n) {}

    [[nodiscard]] std::string join() const {
        if (empty()) {
            return "package";
        }
        return *this | std::views::join_with('.') |
               std::ranges::to<std::string>();
    }
};
struct PackageName : std::vector<std::string_view> {
    using std::vector<std::string_view>::vector;
    PackageName(PackageNameRef ref) { append_range(ref); }

    PackageName(std::string_view package_name) {
        if (package_name.empty()) {
            return;
        }
        for (auto name_part : package_name | std::views::split('.')) {
            emplace_back(name_part);
        }
    }

    operator PackageNameRef() const {
        auto name = std::span(*this);
        return {name};
    }

    [[nodiscard]] std::string join() const {
        return PackageNameRef(*this).join();
    }
};

inline bool operator==(const PackageName& name1, const PackageName& name2) {
    if (name1.size() != name2.size()) {
        return false;
    }
    for (const auto& [i1, i2] : std::views::zip(name1, name2)) {
        if (i1 != i2) {
            return false;
        }
    }
    return true;
}

inline bool operator==(const PackageName& name1, PackageNameRef name2) {
    if (name1.size() != name2.size()) {
        return false;
    }
    for (const auto& [i1, i2] : std::views::zip(name1, name2)) {
        if (i1 != i2) {
            return false;
        }
    }
    return true;
}

inline bool operator==(PackageNameRef name1, PackageNameRef name2) {
    if (name1.size() != name2.size()) {
        return false;
    }
    for (const auto& [i1, i2] : std::views::zip(name1, name2)) {
        if (i1 != i2) {
            return false;
        }
    }
    return true;
}

template <>
struct hash<PackageNameRef> {
    using is_transparent = void;
    std::size_t operator()(PackageNameRef name) const {
        std::size_t result = 0;
        for (auto i : name) {
            hash_combine(result, i);
        }
        return result;
    }
};

}