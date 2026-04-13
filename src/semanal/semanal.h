#pragma once

#include <span>
#include <vector>

#include "../index.h"
#include "errors.h"
#include "parser/nodes.h"
#include "semanal/ir.h"
#include "semanal/types.h"


namespace acu::semanal {
struct ModuleInfo {
    const Source* source;
    const nodes::Module* module;
};

ir::Package resolve(
    std::vector<std::string_view> package_name,
    std::span<const ModuleInfo> modules,
    ErrorHandler& err_handler
);

struct AnalyzedFunc {
    ir::FuncRef ref {};
    IndexVector<types::SpecType, ir::InstRef> inst_types;
};

struct AnalyzedPackage {
    ir::Package ir_package;
    std::vector<AnalyzedFunc> analyzed_funcs;
};

AnalyzedPackage type_analyze(ir::Package package, ErrorHandler& err_handler);
}
