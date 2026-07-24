#pragma once
#include <unordered_map>

#include "errors.h"
#include "parser/nodes.h"

namespace acu::semanal {
struct ModuleContext;
struct UsedItem {
    const ModuleContext* module {};
    utils::Variant<const nodes::Func*, const nodes::Struct*> item;
    Location location;
};
struct UsedModule {
    const ModuleContext* module {};
    std::unordered_map<std::string_view, UsedModule> submodules;
    Location location;
};
namespace builtins {
struct Nothing {};
struct None {};
struct Bool {};
struct Int {
    std::optional<std::uint8_t> bits;
};
struct UInt {
    std::optional<std::uint8_t> bits;
};
struct Float32 {};
struct Float64 {};
struct Array {};
struct Ptr {};
using Type = utils::
    Variant<Nothing, None, Bool, Int, UInt, Float32, Float64, Array, Ptr>;
}

using Symbol = utils::Variant<
    const nodes::Stmt*,
    const nodes::FuncArg*,
    const nodes::Func*,
    const nodes::Struct*,
    const UsedItem*,
    const UsedModule*,
    builtins::Type>;

struct ModuleContext {
    std::vector<std::unique_ptr<UsedItem>> used_items;
    std::unordered_map<std::string_view, UsedModule> used_modules;
    std::unordered_map<const nodes::Expr::Name*, Symbol> var_refs;
    std::unordered_map<
        std::string_view,
        utils::Variant<const nodes::Func*, const nodes::Struct*>>
        public_items;
};
}
