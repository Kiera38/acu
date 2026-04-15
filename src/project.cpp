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
#include <unordered_map>

#include "codegen/generator.h"
#include "codegen/jit.h"
#include "parser/parser.h"
#include "refanal/generator.h"
#include "refanal/ir_str.h"
#include "refanal/optimizer.h"
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

std::vector<std::string_view> split_name(
    const std::string& package_name
) {
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
    const semanal::ProjectContext& context,
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
    std::vector<std::unique_ptr<Package>>& packages
) {
    std::vector<acu::Source> sources;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_directory()) {
            std::string sub_package_name;
            if(package_name.empty()) {
                sub_package_name = entry.path().stem().string();
            } else {
                sub_package_name = package_name + '.' + entry.path().stem().string();
            }
            create_package(
                entry.path(),
                std::move(sub_package_name),
                packages
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
        sources.push_back(read_file(path, package_name));
        sources.back().name = split_name(sources.back().module_name);
    }
    if (sources.empty()) return;
    auto package = std::make_unique<Package>(Package {
        .package_name = std::move(package_name), .sources = std::move(sources)
    });
    package->name = split_name(package->package_name);
    packages.push_back(std::move(package));
}

Project::Project(const std::filesystem::path& input_path) {
    auto [package_path, package_name] =
        [&] -> std::pair<std::filesystem::path, std::string> {
        if (std::filesystem::is_directory(input_path)) {
            return {input_path, input_path.stem().string()};
        } else {
            return {input_path.parent_path(), ""};
        }
    }();
    create_package(package_path, package_name, packages_);
}

void Project::parse(bool show_ast) {
    for (auto& package : packages_) {
        package->modules.reserve(package->sources.size());
        for (auto& source : package->sources) {
            auto module = parse_module(source, err_handler_, show_ast);
            package->modules.push_back(std::move(module));
        }
    }
}


void Project::semanal(bool show_semanal) {
    // todo: sort
    semanal::ProjectContext context;
    for (auto& package : packages_) {
        semanal_package(
            package->package_name, *package, context, err_handler_, show_semanal
        );
        context.add_package(package->name, package->ir_package);
    }
}

void refanal_package(
    Package& package, const acu::refanal::GeneratedModules& modules, acu::ErrorHandler& err_handler, bool show_refanal
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
    for (auto& package : packages_) {
        refanal_package(*package, modules, err_handler_, show_refanal);
        modules.add_module(package->ir_package, package->refanal_module);
    }
}

std::unique_ptr<llvm::Module> generate_llvm(
    llvm::LLVMContext& context,
    const acu::refanal::ir::Module& module,
    llvm::OptimizationLevel opt_level,
    const std::optional<llvm::DataLayout>& layout,
    bool show_llvm,
    bool show_opt_llvm
) {
    auto llvm_module = acu::codegen::generate(context, module, layout);
    if (show_llvm) {
        std::cout << "\nLLVM IR\n";
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
        package.refanal_module,
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
    for (const auto& package : packages_) {
        codegen_package(
            *package,
            show_llvm_ir,
            show_opt_llvm_ir,
            opt,
            output_path / std::format("{}.o", package->package_name)
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
    for (const auto& package : packages_) {
        auto context = std::make_unique<llvm::LLVMContext>();
        auto llvm_module = generate_llvm(
            *context,
            package->refanal_module,
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

}