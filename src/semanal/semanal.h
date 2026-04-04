#pragma once

#include "../index.h"
#include "errors.h"
#include "parser/nodes.h"
#include "semanal/ir.h"
#include "semanal/types.h"

namespace acu::semanal {
ir::Module resolve(const nodes::Module& module, ErrorHandler& err_handler);

struct AnalyzedFunc {
    ir::FuncRef ref{};
    IndexVector<types::SpecType, ir::InstRef> inst_types;
};

struct AnalyzedModule {
    ir::Module ir_module;
    std::vector<AnalyzedFunc> analyzed_funcs;
};

AnalyzedModule type_analyze(
    ir::Module module, const Source& source, ErrorHandler& err_handler
);
}
