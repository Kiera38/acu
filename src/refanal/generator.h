#pragma once

#include "refanal/ir.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"

namespace acu::refanal {

ir::Module generate(const semanal::AnalyzedModule& analyzed_module);

}
