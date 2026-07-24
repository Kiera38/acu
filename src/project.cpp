#include "project.h"

#include <print>

#include "parser/parser.h"
#include "semanal/context.h"
#include "semanal/resolve.h"

namespace acu {
namespace fs = std::filesystem;

Project::Project(std::vector<std::filesystem::path> search_paths)
    : search_paths(std::move(search_paths)) {}

void Project::analyze(const std::filesystem::path& path) {
    auto module_name = path.stem().string();
    load_module(path, module_name);
    auto& module =
        modules.at(ModuleNameRef{std::array {std::string_view {module_name}}});
}

void Project::load_module(const std::filesystem::path& path, std::string name) {
    auto module =
        std::make_unique<Module>(Source {std::move(name), std::move(path)});
    module->module = parser::parse(module->source, err_handler);
    semanal::resolve(module->module, *this);
    modules.insert_or_assign(module->source.module_name(), std::move(module));
}

void Project::add_module(
    ModuleNameRef module_name, const Source& source, Location location
) {
    if (modules.contains(module_name)) return;
    modules.emplace(module_name, nullptr);
    auto relative_path = fs::path(module_name.front());
    for (auto part : module_name.subspan(1)) {
        relative_path = relative_path / part;
    }
    auto relative_package_path = relative_path / "package.acu";
    relative_path = relative_path.replace_extension(".acu");
    for (const auto& search_path : search_paths) {
        auto path = search_path / relative_path;
        if (fs::exists(path)) {
            load_module(path, module_name.join());
            return;
        }
        path = search_path / relative_package_path;
        if (fs::exists(path)) {
            load_module(path, module_name.join());
            return;
        }
    }
    err_handler.report({
        .message = std::format("module '{}' not found", module_name.join()),
        .labels = {
            {.source = &source,
             .location = location,
             .message = "search this module"}
        },
    });
}

}
