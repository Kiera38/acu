#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "parser/nodes.h"
#include "project.h"
#include "resolver.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"
#include "semanal/types.h"
#include "source.h"
#include "variant.h"

namespace acu::semanal {

ir::Package resolve(
    PackageName package_name,
    std::span<const ModuleRef> modules,
    const Packages& context,
    ErrorHandler& err_handler
) {
    Resolver resolver(std::move(package_name), modules, context, err_handler);
    return resolver.resolve();
}

std::vector<std::pair<PackageNameRef, Location>>
get_module_usings(const nodes::Module& module) {
    std::vector<std::pair<PackageNameRef, Location>> usings;
    for (const auto& item : module.items) {
        item.data.visit(
            [&](const nodes::Use& use) {
                usings.emplace_back(use.module_name, item.location);
            },
            [&](const nodes::FromUse& use) {
                usings.emplace_back(use.module_name, item.location);
            },
            [&](const auto&) {}
        );
    }
    return usings;
}

}

namespace acu::ir {
utils::Variant<std::monostate, FuncRef, types::TypeId> Module::find(
    std::string_view name
) const {
    if (auto it = public_funcs_.find(name); it != public_funcs_.end()) {
        return it->second;
    }
    if (auto it = structs_.find(name); it != structs_.end()) {
        return it->second;
    }
    return std::monostate {};
}
}