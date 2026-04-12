#pragma once

#include "refanal/ir.h"
#include "semanal/semanal.h"

namespace acu::refanal {

ir::Module generate(semanal::AnalyzedPackage& analyzed_package);

}
