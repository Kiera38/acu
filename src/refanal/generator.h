#pragma once

#include "refanal/ir.h"
#include "semanal/ir.h"

namespace acu::refanal {
ir::Module generate(acu::ir::AnalyzedPackage& analyzed_package);
}
