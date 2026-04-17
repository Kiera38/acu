#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "errors.h"
#include "parser/nodes.h"
#include "semanal/ir.h"
#include "project.h"

namespace acu::semanal {

std::vector<std::pair<std::span<const std::string_view>, Location>>
get_module_usings(const nodes::Module& module);

ir::Package resolve(
    std::vector<std::string_view> package_name,
    std::span<const ModuleRef> modules,
    const Packages& context,
    ErrorHandler& err_handler
);

ir::AnalyzedPackage type_analyze(ir::Package& package, ErrorHandler& err_handler);
}
