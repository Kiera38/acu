#pragma once

#include "parser/nodes.h"
#include "semanal/ir.h"
#include "semanal/types.h"


namespace acu::semanal {
ir::Module resolve(const nodes::Module& module);

struct AnalyzedFunc {
    ir::FuncRef ref;
    std::vector<types::TypeId> inst_types;
};

struct AnalyzedModule {
    ir::Module ir_module;
    std::vector<AnalyzedFunc> analyzed_funcs;
};

AnalyzedModule type_analyze(ir::Module module, const Source& source);
}
