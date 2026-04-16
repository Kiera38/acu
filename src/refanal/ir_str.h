#pragma once

#include <string>

#include "refanal/ir.h"
#include "semanal/ir.h"

namespace acu::refanal {

std::string to_string(
    const ir::Module& module, const acu::ir::AnalyzedPackage& analyzed
);
std::string to_string(
    const ir::Func& func, const acu::ir::AnalyzedPackage& analyzed
);

}  // namespace acu::refanal
