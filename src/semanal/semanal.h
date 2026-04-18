#pragma once

#include <span>
#include <vector>

#include "errors.h"
#include "package_name.h"
#include "parser/nodes.h"
#include "project.h"
#include "semanal/ir.h"


namespace acu::semanal {

std::vector<std::pair<PackageNameRef, Location>> get_module_usings(
    const nodes::Module& module
);

ir::Package resolve(
    PackageName package_name,
    std::span<const ModuleRef> modules,
    const Packages& context,
    ErrorHandler& err_handler
);

ir::AnalyzedPackage type_analyze(
    ir::Package& package, ErrorHandler& err_handler
);
}
