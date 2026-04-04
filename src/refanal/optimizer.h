#pragma once

#include "refanal/ir.h"
#include "semanal/semanal.h"

namespace acu::refanal {

void optimize(
    ir::Module& module,
    semanal::AnalyzedModule& analyzed,
    ErrorHandler& err_handler
);

}  // namespace acu::refanal
