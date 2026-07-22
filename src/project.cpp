#include "project.h"

#include <print>

#include "parser/parser.h"

namespace acu {
namespace fs = std::filesystem;

namespace {
void load_sources(
    const fs::path& package,
    std::vector<Source>& sources,
    const std::string& package_name
) {
    for (const auto& file : package) {
        std::string module_name = file.stem().string();
        if (package_name.empty()) {
            if (module_name == "package") {
                module_name = "";
            }
        } else {
            if (module_name == "package") {
                module_name = package_name;
            } else {
                std::string name;
                name.reserve(package_name.size() + module_name.size() + 1);
                name.append(package_name).append(".").append(module_name);
                module_name = std::move(name);
            }
        }
        if (fs::is_directory(file)) {
            load_sources(file, sources, module_name);
        } else if (file.extension() == ".acu") {
            sources.emplace_back(module_name, file);
        }
    }
}
}

Project::Project(const fs::path& project_path) {
    auto path = fs::absolute(project_path);
    if (fs::is_directory(path)) {
        load_sources(project_path, sources_, path.stem().string());
    } else if (path.extension() == ".acu") {
        load_sources(path.parent_path(), sources_, "");
    }
}

bool Project::parse(bool show_ast) {
    ErrorHandler err_handler;
    for (auto& source : sources_) {
        modules_.emplace_back(parser::parse(source, err_handler));
    }
    for (const auto& module : modules_) {
        if (auto it = module_map_.find(module.source->module_name());
            it != module_map_.end()) {
            err_handler.report({
                .severity = Severity::Error,
                .message = std::format(
                    "несколько модулей с названием '{}'", module.source->name()
                ),
                .notes = {
                    module.source->path().string(),
                    it->second->source->path().string()
                },
            });
        }
        module_map_.emplace(module.source->module_name(), &module);
    }
    if (show_ast) {
        std::println("AST:");
        for (const auto& module : modules_) {
            std::println("{}", module.source->path().string());
            std::println("{}", nodes::to_string(module));
        }
    }
    if (err_handler.has_errors()) {
        err_handler.emit_all();
        return false;
    }
    return true;
}
}
