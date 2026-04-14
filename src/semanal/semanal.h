#pragma once

#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../index.h"
#include "errors.h"
#include "parser/nodes.h"
#include "semanal/ir.h"
#include "semanal/types.h"

namespace acu::semanal {
template <class T>
static void hash_combine(std::size_t& seed, const T& v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct PackageNameHash {
    std::size_t operator()(std::span<const std::string_view> name) {
        std::size_t result {};
        for (auto i : name) {
            hash_combine(result, i);
        }
        return result;
    }
};
class ProjectContext {
public:
    ProjectContext() = default;
    [[nodiscard]] const ir::Package* get_package(
        std::span<const std::string_view> name
    ) const {
        if (auto it = packages_.find(name); it != packages_.end()) {
            return it->second;
        }
        return nullptr;
    }
    void add_package(
        std::span<const std::string_view> name, const ir::Package& package
    ) {
        packages_.insert({name, &package});
    }

private:
    std::unordered_map<
        std::span<const std::string_view>,
        const ir::Package*,
        PackageNameHash>
        packages_;
};

std::unordered_set<std::span<const std::string_view>, PackageNameHash>
get_module_usings(const nodes::Module& module);

ir::Package resolve(
    std::vector<std::string_view> package_name,
    std::span<const nodes::Module> modules,
    const ProjectContext& context,
    ErrorHandler& err_handler
);

struct AnalyzedFunc {
    ir::FuncRef ref {};
    IndexVector<types::SpecType, ir::InstRef> inst_types;
};

struct AnalyzedPackage {
    ir::Package* ir_package;
    std::vector<AnalyzedFunc> analyzed_funcs;
};

AnalyzedPackage type_analyze(ir::Package& package, ErrorHandler& err_handler);
}
