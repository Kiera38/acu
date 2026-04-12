#pragma once

#include "../index.h"
#include "errors.h"
#include "parser/nodes.h"
#include "semanal/ir.h"
#include "semanal/types.h"

namespace acu::semanal {
ir::Package resolve(const nodes::Module& module, ErrorHandler& err_handler);

struct AnalyzedFunc {
    ir::FuncRef ref{};
    IndexVector<types::SpecType, ir::InstRef> inst_types;
};

struct AnalyzedPackage {
    ir::Package ir_package;
    std::vector<AnalyzedFunc> analyzed_funcs;
};

AnalyzedPackage type_analyze(
    ir::Package package, const Source& source, ErrorHandler& err_handler
);
}
