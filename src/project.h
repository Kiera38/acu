#pragma once
#include <filesystem>

#include "errors.h"
#include "hash.h"
#include "index.h"
#include "module_name.h"
#include "parser/nodes.h"
// #include "refanal/ir.h"
#include "source.h"

namespace acu {
enum class OptimizationLevel : std::uint8_t { O0, O1, O2, O3, Os, Oz };

class Project {
public:
    explicit Project(const std::filesystem::path& project_path);
    bool parse(bool show_ast);
    void semanal();
    void refanal();
    void codegen();

private:
    std::vector<Source> sources_;
    std::vector<nodes::Module> modules_;
    std::unordered_map<ModuleNameRef, const nodes::Module*, hash<ModuleNameRef>>
        module_map_;
};

}