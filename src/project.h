#pragma once
#include <filesystem>

#include "errors.h"
#include "hash.h"
#include "index.h"
#include "module_name.h"
#include "parser/nodes.h"
// #include "refanal/ir.h"
#include "semanal/context.h"
#include "source.h"

namespace acu {
enum class OptimizationLevel : std::uint8_t { O0, O1, O2, O3, Os, Oz };

struct Module {
    Source source;
    nodes::Module module;
    semanal::ModuleContext context;
};

class Project {
public:
    explicit Project(std::vector<std::filesystem::path> search_paths);

    void analyze(const std::filesystem::path& path);
    void add_module(
        ModuleNameRef module_name, const Source& source, Location using_location
    );

    [[nodiscard]] const semanal::ModuleContext& module_context(ModuleNameRef name) const {
        return modules.at(name)->context;
    }

    ErrorHandler& err_handler() {return err_handler_;}

private:
    void load_module(std::filesystem::path path, std::string name);

    std::vector<std::filesystem::path> search_paths;
    ErrorHandler err_handler_;
    std::unordered_map<
        ModuleNameRef,
        std::unique_ptr<Module>,
        hash<ModuleNameRef>>
        modules;
};

}