#pragma once

#include "refanal/ir.h"
#include "semanal/ir.h"
#include "errors.h"

namespace acu::refanal {

void check(
    ir::Module& module,
    ::acu::ir::AnalyzedPackage& analyzed,
    ErrorHandler& err_handler
);

}
