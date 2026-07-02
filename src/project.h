#pragma once
#include <filesystem>
#include <string>

#include "errors.h"
#include "hash.h"
#include "index.h"
#include "package_name.h"
#include "parser/nodes.h"
// #include "refanal/ir.h"
#include "source.h"

namespace acu {
enum class OptimizationLevel : std::uint8_t { O0, O1, O2, O3, Os, Oz };

struct Package {
    std::string name;
    PackageName package_name;
    std::vector<Source> sources;
    std::vector<std::unique_ptr<nodes::Module>> modules;
    const nodes::Module* root_module;
    // refanal::ir::Module ir_module;
};

class Project {
public:
    explicit Project(const std::filesystem::path& project_path);
    void parse();
    void semanal();
    void refanal();
    void codegen();

private:
    std::vector<Package> packages;
    std::unordered_map<PackageNameRef, const Package*, hash<PackageNameRef>>
        package_map;
    std::unordered_map<const Package*, std::vector<const Package*>>
        package_deps;
    std::vector<const Package*> sorted_packages;
};

}