#pragma once

#include "refanal/ir.h"
#include "semanal/semanal.h"

namespace acu::refanal {

ir::Module generate(semanal::AnalyzedModule& analyzed_module);

}
