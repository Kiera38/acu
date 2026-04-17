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
#include "parser/parser.h"
#include "refanal/generator.h"
#include "refanal/ir_str.h"
#include "refanal/optimizer.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"

namespace acu {

acu::Source read_file(
    const std::filesystem::path& path, const std::string& package_name
) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: could not open file: " << path << "\n";
        throw std::runtime_error("");
    }

    std::stringstream ss;
    ss << file.rdbuf();
    std::string module_name = path.stem().string();
    if (module_name == "package") {
        module_name = package_name;
    } else if (!package_name.empty()) {
        module_name = package_name + '.' + module_name;
    }

    return acu::Source {
        .module_name = module_name, .path = path.string(), .content = ss.str()
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

std::vector<std::string_view> split_name(const std::string& package_name) {
    if (package_name.empty()) {
        return {};
    }
    std::vector<std::string_view> name;
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
            std::string sub_package_name;
            if (package_name.empty()) {
                sub_package_name = entry.path().stem().string();
            } else {
                sub_package_name =
                    package_name + '.' + entry.path().stem().string();
            }
            create_package(
                entry.path(), std::move(sub_package_name), packages, modules
            );
            continue;
        } else if (!entry.is_regular_file()) {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension().string() != ".acu") {
            continue;
        }
        if (path.stem().string() == "package" && package_name.empty()) {
            continue;
        }
        Module module {.source = read_file(path, package_name)};
        module_refs.push_back(
            modules.emplace_back(read_file(path, package_name), nullptr)
        );
    }
    if (module_refs.empty()) return;
    auto package = Package {
        .package_name = std::move(package_name),
        .modules = std::move(module_refs)
    };
    packages.push_back(std::move(package));
}

Project::Project(const std::filesystem::path& input_path) {
    auto [package_path, package_name] =
        [&] -> std::pair<std::filesystem::path, std::string> {
        if (std::filesystem::is_directory(input_path)) {
            return {input_path, ""};
        } else {
            return {input_path.parent_path(), ""};
        }
    }();
    create_package(package_path, package_name, packages_, modules_);
    for (auto& module : modules_) {
        module.source.name = split_name(module.source.module_name);
    }
    for (auto& package : packages_) {
        package.name = split_name(package.package_name);
    }
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
        throw std::runtime_error("");
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
    Package& package,
    const acu::refanal::GeneratedModules& modules,
    acu::ErrorHandler& err_handler,
    bool show_refanal
) {
    package.refanal_module = acu::refanal::generate(package.analyzed, modules);
    acu::refanal::optimize(
        package.refanal_module, package.analyzed, err_handler
    );

    if (err_handler.has_errors()) {
        err_handler.emit_all();
        throw std::runtime_error("");
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
    acu::refanal::GeneratedModules modules;
    for (auto ref : sorted_packages_) {
        auto& package = packages_[ref];
        refanal_package(package, modules, err_handler_, show_refanal);
        modules.add_module(package.ir_package, package.refanal_module);
    }
}

std::unique_ptr<llvm::Module> generate_llvm(
    llvm::LLVMContext& context,
    const Package& package,
    llvm::OptimizationLevel opt_level,
    const std::optional<llvm::DataLayout>& layout,
    bool show_llvm,
    bool show_opt_llvm
) {
    auto llvm_module = acu::codegen::generate(context, package.refanal_module, layout);
    if (show_llvm) {
        std::cout << "\nLLVM IR\n";
        std::cout << package.package_name << '\n';
        llvm_module->print(llvm::outs(), nullptr);
    }

    acu::codegen::optimize(*llvm_module, opt_level);
    if (show_opt_llvm) {
        std::cout << "\nOPTIMIZED LLVM IR\n";
        llvm_module->print(llvm::outs(), nullptr);
    }
    return llvm_module;
}

void codegen_package(
    const Package& package,
    bool show_llvm_ir,
    bool show_opt_llvm_ir,
    llvm::OptimizationLevel opt,
    const std::filesystem::path& output_path
) {
    auto context = std::make_unique<llvm::LLVMContext>();
    auto llvm_module = generate_llvm(
        *context,
        package,
        opt,
        std::nullopt,
        show_llvm_ir,
        show_opt_llvm_ir
    );
    acu::codegen::emit_object_file(*llvm_module, output_path.string());
}

void Project::codegen(
    bool show_llvm_ir,
    bool show_opt_llvm_ir,
    llvm::OptimizationLevel opt,
    const std::filesystem::path& output_path
) {
    for (auto ref : sorted_packages_) {
        const auto& package = packages_[ref];
        codegen_package(
            package,
            show_llvm_ir,
            show_opt_llvm_ir,
            opt,
            output_path / std::format("{}.o", package.package_name)
        );
    }
}

void Project::run_jit(
    bool show_llvm_ir, bool show_opt_llvm_ir, llvm::OptimizationLevel opt
) {
    std::cout << "\nJIT EXECUTION\n";
    auto jit = acu::codegen::JIT::create();
    if (!jit) {
        std::cerr << "Failed to create JIT\n";
        throw std::runtime_error("");
    }
    for (auto ref : sorted_packages_) {
        const auto& package = packages_[ref];
        auto context = std::make_unique<llvm::LLVMContext>();
        auto llvm_module = generate_llvm(
            *context,
            package,
            opt,
            jit->get_data_layout(),
            show_llvm_ir,
            show_opt_llvm_ir
        );
        if (auto err =
                jit->add_module(std::move(llvm_module), std::move(context))) {
            std::cerr << "Failed to add module to JIT\n";
            throw std::runtime_error("");
        }
    }

    auto main_func = jit->get_main();
    if (main_func) {
        std::cout << "Running main()...\n";
        int result = (*main_func)();
        std::cout << "main() returned: " << result << "\n";
    } else {
        std::cerr << "Failed to find main function in JIT\n";
        throw std::runtime_error("");
    }
}

std::span<const std::string_view> get_package_name(const Module& module) {
    if (module.source.path.filename().string() == "package.acu") {
        return module.source.name;
    }
    return std::span(module.source.name)
        .subspan(0, module.source.name.size() - 1);
}

std::span<const std::string_view> Packages::package_name(
    std::span<const std::string_view> module_name
) const {
    return get_package_name(project_->modules_[modules_.at(module_name)]);
}

std::pair<ir::Package*, ir::Module*> Packages::module_package(
    std::span<const std::string_view> module_name
) const {
    auto& module = project_->modules_[modules_.at(module_name)];
    auto package_name = get_package_name(module);
    auto& package = project_->packages_[packages_.at(package_name)];
    if(package_name.size() == module_name.size()) {
        auto& ir_module = package.ir_package.root_module();
        return {&package.ir_package, &ir_module};
    }
    auto& ir_module = package.ir_package.module(module_name.back());
    return {&package.ir_package, &ir_module};
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
                    if (!PackageNameEqual {}(package_name, package.name)) {
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