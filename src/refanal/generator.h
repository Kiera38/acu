#pragma once

#include <unordered_map>
#include "refanal/ir.h"
#include "semanal/ir.h"

namespace acu::refanal {

class GeneratedModules {
public:
    GeneratedModules() = default;
    [[nodiscard]] const ir::Module& get(const acu::ir::Package& package) const {
        return *modules_.at(&package);
    }
    void add_module(const acu::ir::Package& package, const ir::Module& module) {
        modules_.insert({&package, &module});
    }
private:
    std::unordered_map<const acu::ir::Package*, const ir::Module*> modules_;
};

ir::Module generate(acu::ir::AnalyzedPackage& analyzed_package, const GeneratedModules& modules);

}
