#pragma once

#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "hash.h"

namespace acu {
struct ModuleNameRef : std::span<const std::string_view> {
    ModuleNameRef() = default;
    explicit ModuleNameRef(std::span<const std::string_view> n)
        : std::span<const std::string_view>(n) {}

    [[nodiscard]] std::string join() const {
        if (empty()) {
            return "package";
        }
        return *this | std::views::join_with('.') |
               std::ranges::to<std::string>();
    }
    [[nodiscard]] ModuleNameRef parent() const {
        return ModuleNameRef(subspan(0, size()-1));
    }
};
struct ModuleName : std::vector<std::string_view> {
    using std::vector<std::string_view>::vector;
    explicit ModuleName(ModuleNameRef ref) { append_range(ref); }

    explicit ModuleName(std::string_view package_name) {
        if (package_name.empty()) {
            return;
        }
        for (auto name_part : package_name | std::views::split('.')) {
            emplace_back(name_part);
        }
    }

    operator ModuleNameRef() const {
        auto name = std::span(*this);
        return ModuleNameRef{name};
    }

    [[nodiscard]] std::string join() const {
        return ModuleNameRef(*this).join();
    }
};

inline bool operator==(ModuleNameRef name1, ModuleNameRef name2) {
    return std::ranges::equal(name1, name2);
}

inline bool operator==(const ModuleName& name1, const ModuleName& name2) {
    return ModuleNameRef(name1) == ModuleNameRef(name2);
}

inline bool operator==(const ModuleName& name1, ModuleNameRef name2) {
    return ModuleNameRef(name1) == name2;
}

template <>
struct hash<ModuleNameRef> {
    using is_transparent = void;
    std::size_t operator()(ModuleNameRef name) const {
        std::size_t result = 0;
        for (auto i : name) {
            hash_combine(result, i);
        }
        return result;
    }
};

}