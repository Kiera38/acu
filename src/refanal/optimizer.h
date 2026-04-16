#pragma once

#include "errors.h"
#include "refanal/ir.h"
#include "semanal/ir.h"

namespace acu::refanal {

void optimize(
    ir::Module& module,
    acu::ir::AnalyzedPackage& analyzed,
    ErrorHandler& err_handler
);

}  // namespace acu::refanal
