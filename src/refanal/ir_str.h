#pragma once

#include <string>

#include "refanal/ir.h"
#include "semanal/semanal.h"

namespace acu::refanal {

std::string to_string(
    const ir::Module& module, const semanal::AnalyzedModule& analyzed
);
std::string to_string(
    const ir::Func& func, const semanal::AnalyzedModule& analyzed
);

}  // namespace acu::refanal
