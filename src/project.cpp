#include "project.h"

#include <llvm/IR/Module.h>
#include <llvm/Passes/OptimizationLevel.h>

#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "codegen/generator.h"
#include "codegen/jit.h"
#include "parser/parser.h"
#include "refanal/generator.h"
#include "refanal/ir_str.h"
#include "refanal/optimizer.h"

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

std::vector<std::string_view> split_package_name(
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
    acu::ErrorHandler& err_handler,
    bool show_semanal
) {
    package.ir_package = acu::semanal::resolve(
        split_package_name(package_name), package.modules, err_handler
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

Project::Project(const std::filesystem::path& input_path) {
    std::vector<acu::Source> sources;
    auto [package_path, package_name] =
        [&] -> std::pair<std::filesystem::path, std::string> {
        if (std::filesystem::is_directory(input_path)) {
            return {input_path, input_path.stem().string()};
        } else {
            return {input_path.parent_path(), ""};
        }
    }();
    for (const auto& entry :
         std::filesystem::directory_iterator(package_path)) {
        if (!entry.is_regular_file()) {
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
    }
    packages_.insert(
        {std::move(package_name), Package {.sources = std::move(sources)}}
    );
}

void Project::parse(bool show_ast) {
    for (auto& [name, package] : packages_) {
        for (auto& source : package.sources) {
            auto module = parse_module(source, err_handler_, show_ast);
            package.modules.push_back(std::move(module));
        }
    }
}

void Project::semanal(bool show_semanal) {
    for (auto& [name, package] : packages_) {
        semanal_package(name, package, err_handler_, show_semanal);
    }
}

void refanal_package(
    Package& package, acu::ErrorHandler& err_handler, bool show_refanal
) {
    package.refanal_module = acu::refanal::generate(package.analyzed);
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
    for (auto& [name, package] : packages_) {
        refanal_package(package, err_handler_, show_refanal);
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
    for (const auto& [name, package] : packages_) {
        codegen_package(
            package,
            show_llvm_ir,
            show_opt_llvm_ir,
            opt,
            output_path / std::format("{}.o", name)
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
    for (const auto& [name, package] : packages_) {
        auto context = std::make_unique<llvm::LLVMContext>();
        auto llvm_module = generate_llvm(
            *context,
            package.refanal_module,
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