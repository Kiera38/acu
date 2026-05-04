#include "project.h"

#include <llvm/IR/Module.h>
#include <llvm/Passes/OptimizationLevel.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string_view>

#include "codegen/generator.h"
#include "codegen/jit.h"
#include "package_name.h"
#include "parser/parser.h"
#include "refanal/generator.h"
#include "refanal/ir_str.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"

namespace acu {

acu::Source read_file(
    const std::filesystem::path& path, std::string_view package_name
) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error(
            std::format("could not open file: {}", path.string())
        );
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string module_name = path.stem().string();
    if (module_name == "package") {
        module_name = std::string(package_name);
    } else if (!package_name.empty()) {
        module_name = std::format("{}.{}", package_name, module_name);
    }

    return acu::Source {
        .module_name = std::move(module_name), .path = path, .content = ss.str()
    };
}

acu::nodes::Module parse_module(
    acu::Source& source, acu::ErrorHandler& err_handler, bool show_ast
) {
    auto module = acu::parser::parse(source, err_handler);
    if (err_handler.has_errors()) {
        err_handler.emit_all();
        throw std::runtime_error("");
    }
    if (show_ast) {
        std::cout << "\nAST for " << source.module_name << "\n";
        std::cout << acu::nodes::to_string(module);
    }
    return module;
}

PackageName split_name(const std::string& package_name) {
    if (package_name.empty()) {
        return {};
    }
    PackageName name;
    for (auto name_part : package_name | std::views::split('.')) {
        name.emplace_back(name_part);
    }
    return name;
}

void semanal_package(
    const std::string& package_name,
    Package& package,
    const Packages& context,
    acu::ErrorHandler& err_handler,
    bool show_semanal
) {
    package.ir_package = acu::semanal::resolve(
        split_name(package_name), package.modules, context, err_handler
    );
    if (err_handler.has_errors()) {
        err_handler.emit_all();
        throw std::runtime_error("");
    }
    if (show_semanal) {
        std::cout << "\nSEMANAL IR\n";
        std::cout << acu::ir::to_string(package.ir_package);
    }

    package.analyzed =
        acu::semanal::type_analyze(package.ir_package, err_handler);
    if (err_handler.has_errors()) {
        err_handler.emit_all();
        throw std::runtime_error("");
    }
}

void create_package(
    const std::filesystem::path& path,
    std::string package_name,
    IndexVector<Package, PackageRef>& packages,
    IndexVector<Module, ModuleRef>& modules
) {
    std::vector<ModuleRef> module_refs;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_directory()) {
            std::string sub_package_name =
                package_name.empty()
                    ? entry.path().stem().string()
                    : std::format(
                          "{}.{}", package_name, entry.path().stem().string()
                      );
            create_package(
                entry.path(), std::move(sub_package_name), packages, modules
            );
            continue;
        }

        if (!entry.is_regular_file() || entry.path().extension() != ".acu") {
            continue;
        }

        auto source = read_file(entry.path(), package_name);
        source.name = split_name(source.module_name);
        module_refs.push_back(modules.emplace_back(std::move(source), nullptr));
    }

    if (module_refs.empty()) return;

    auto name = split_name(package_name);
    packages.emplace_back(
        std::move(package_name), std::move(name), std::move(module_refs)
    );
}

Project::Project(const std::filesystem::path& input_path) {
    std::filesystem::path package_path =
        std::filesystem::is_directory(input_path) ? input_path
                                                  : input_path.parent_path();

    create_package(package_path, "", packages_, modules_);
}

void Project::parse(bool show_ast) {
    for (auto& package : packages_) {
        for (auto ref : package.modules) {
            auto& module = modules_[ref];
            module.module = std::make_unique<nodes::Module>(
                parse_module(module.source, err_handler_, show_ast)
            );
        }
    }
}

void Project::semanal(bool show_semanal) {
    Packages context(*this);
    if (err_handler_.has_errors()) {
        err_handler_.emit_all();
        throw std::runtime_error(
            "semantic analysis failed due to previous errors"
        );
    }
    sorted_packages_ = context.sort();
    for (auto ref : sorted_packages_) {
        auto& package = packages_[ref];
        semanal_package(
            package.package_name, package, context, err_handler_, show_semanal
        );
    }
}

void refanal_package(
    Package& package, acu::ErrorHandler& err_handler, bool show_refanal
) {
    package.refanal_module = acu::refanal::generate(package.analyzed);
    if (err_handler.has_errors()) {
        err_handler.emit_all();
        throw std::runtime_error("reference analysis failed");
    }

    if (show_refanal) {
        std::cout << "\nREFANAL IR\n";
        std::cout << acu::refanal::to_string(
                         package.refanal_module, package.analyzed
                     )
                  << "\n";
    }
}

void Project::refanal(bool show_refanal) {
    for (auto ref : sorted_packages_) {
        auto& package = packages_[ref];
        refanal_package(package, err_handler_, show_refanal);
    }
    if (err_handler_.has_errors()) {
        err_handler_.emit_all();
        throw std::runtime_error("reference analysis failed");
    }
}

std::unique_ptr<llvm::Module> generate_llvm(
    llvm::LLVMContext& context,
    const Package& package,
    const Project& project,
    acu::OptimizationLevel opt_level,
    const std::optional<llvm::DataLayout>& layout,
    bool show_llvm,
    bool show_opt_llvm
) {
    auto llvm_module = acu::codegen::generate(
        context, package.refanal_module, project, layout
    );
    if (show_llvm) {
        std::cout << "\nLLVM IR\n";
        std::cout << package.package_name << '\n';
        llvm_module->print(llvm::outs(), nullptr);
    }

    auto llvm_opt_level = [&] {
        switch (opt_level) {
            case acu::OptimizationLevel::O0: return llvm::OptimizationLevel::O0;
            case acu::OptimizationLevel::O1: return llvm::OptimizationLevel::O1;
            case acu::OptimizationLevel::O2: return llvm::OptimizationLevel::O2;
            case acu::OptimizationLevel::O3: return llvm::OptimizationLevel::O3;
            case acu::OptimizationLevel::Os: return llvm::OptimizationLevel::Os;
            case acu::OptimizationLevel::Oz: return llvm::OptimizationLevel::Oz;
        }
    }();

    acu::codegen::optimize(*llvm_module, llvm_opt_level);
    if (show_opt_llvm) {
        std::cout << "\nOPTIMIZED LLVM IR\n";
        llvm_module->print(llvm::outs(), nullptr);
    }
    return llvm_module;
}

void Project::codegen(
    bool show_llvm_ir,
    bool show_opt_llvm_ir,
    acu::OptimizationLevel opt,
    const std::filesystem::path& output_path
) {
    if (!std::filesystem::exists(output_path)) {
        std::filesystem::create_directories(output_path);
    }

    llvm::LLVMContext context;
    for (auto ref : sorted_packages_) {
        const auto& package = packages_[ref];
        std::string file_name = package.package_name.empty()
                                    ? "package.o"
                                    : std::format("{}.o", package.package_name);

        auto llvm_module = generate_llvm(
            context,
            package,
            *this,
            opt,
            std::nullopt,
            show_llvm_ir,
            show_opt_llvm_ir
        );
        acu::codegen::emit_object_file(
            *llvm_module, (output_path / file_name).string()
        );
    }
}

void Project::run_jit(
    bool show_llvm_ir, bool show_opt_llvm_ir, acu::OptimizationLevel opt
) {
    std::cout << "\nJIT EXECUTION\n";
    auto jit = acu::codegen::JIT::create();
    if (!jit) {
        throw std::runtime_error("failed to create JIT");
    }

    auto ts_context =
        llvm::orc::ThreadSafeContext(std::make_unique<llvm::LLVMContext>());
    for (auto ref : sorted_packages_) {
        const auto& package = packages_[ref];
        ts_context.withContextDo([&](llvm::LLVMContext* context) {
            auto llvm_module = generate_llvm(
                *context,
                package,
                *this,
                opt,
                jit->get_data_layout(),
                show_llvm_ir,
                show_opt_llvm_ir
            );

            if (auto err = jit->add_module(
                    llvm::orc::ThreadSafeModule(
                        std::move(llvm_module), ts_context
                    )
                )) {
                throw std::runtime_error("failed to add module to JIT");
            }
        });
    }

    auto main_func = jit->get_main();
    if (main_func) {
        std::cout << "Running main()...\n";
        int result = (*main_func)();
        std::cout << "main() returned: " << result << "\n";
    } else {
        throw std::runtime_error("failed to find main function in JIT");
    }
}

PackageNameRef get_package_name(const Module& module) {
    if (module.source.path.filename().string() == "package.acu") {
        return module.source.name;
    }
    return PackageNameRef(module.source.name)
        .subspan(0, module.source.name.size() - 1);
}

PackageNameRef Packages::package_name(PackageNameRef module_name) const {
    return get_package_name(project_->modules_[modules_.at(module_name)]);
}

std::pair<ir::PackageRef, ir::Module*> Packages::module_package(
    PackageNameRef module_name
) const {
    auto& module = project_->modules_[modules_.at(module_name)];
    auto package_name = get_package_name(module);
    auto package_ref = packages_.at(package_name);
    auto& package = project_->packages_[package_ref];
    if (package_name.size() == module_name.size()) {
        auto& ir_module = package.ir_package.root_module();
        return {ir::PackageRef {package_ref.index}, &ir_module};
    }
    auto& ir_module = package.ir_package.module(module_name.back());
    return {ir::PackageRef {package_ref.index}, &ir_module};
}

std::string join_module_name(std::span<const std::string_view> module_name) {
    return module_name | std::views::join_with('.') |
           std::ranges::to<std::string>();
}

Packages::Packages(Project& project) : project_(&project) {
    for (auto ref : project.modules_.indices()) {
        modules_.insert({project.modules_[ref].source.name, ref});
    }
    for (auto ref : project.packages_.indices()) {
        packages_.insert({project.packages_[ref].name, ref});
    }
    for (auto ref : project.packages_.indices()) {
        const auto& package = project.packages_[ref];
        std::unordered_set<PackageRef, hash<PackageRef>, equal_to<PackageRef>>
            usings;
        for (auto module_ref : project.packages_[ref].modules) {
            const auto& module = project.modules_[module_ref];
            auto module_usings = semanal::get_module_usings(*module.module);
            for (auto module_name : module_usings) {
                auto it = modules_.find(module_name.first);
                if (it != modules_.end()) {
                    auto package_name =
                        get_package_name(project.modules_[it->second]);
                    if (package_name != package.name) {
                        usings.emplace(packages_[package_name]);
                    }
                } else {
                    project.err_handler_.error(
                        module.source,
                        module_name.second,
                        std::format(
                            "module '{}' not found",
                            join_module_name(module_name.first)
                        )
                    );
                }
            }
        }
        package_usings_.push_back(std::move(usings));
    }
}

std::vector<PackageRef> Packages::sort() {
    IndexVector<std::vector<PackageRef>, PackageRef> dependents(
        project_->packages_.size()
    );
    IndexVector<std::size_t, PackageRef> indegree(project_->packages_.size());

    for (auto package_ref : project_->packages_.indices()) {
        for (auto dependency : package_usings_[package_ref]) {
            dependents[dependency].push_back(package_ref);
            ++indegree[package_ref];
        }
    }

    std::vector<PackageRef> order;
    order.reserve(project_->packages_.size());
    std::vector<PackageRef> queue;
    queue.reserve(project_->packages_.size());

    for (auto package_ref : project_->packages_.indices()) {
        if (indegree[package_ref] == 0) {
            queue.push_back(package_ref);
        }
    }

    for (std::size_t i = 0; i < queue.size(); ++i) {
        auto package_ref = queue[i];
        order.push_back(package_ref);
        for (auto dependent : dependents[package_ref]) {
            auto& count = indegree[dependent];
            --count;
            if (count == 0) {
                queue.push_back(dependent);
            }
        }
    }

    if (order.size() != project_->packages_.size()) {
        throw std::runtime_error("circular package dependency detected");
    }

    return order;
}

}