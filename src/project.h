#pragma once
#include <llvm/Passes/OptimizationLevel.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "errors.h"
#include "parser/nodes.h"
#include "refanal/ir.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"
#include "source.h"

namespace acu {
struct Package {
    std::string package_name;
    std::vector<std::string_view> name;
    std::vector<Source> sources;
    std::vector<nodes::Module> modules;
    ir::Package ir_package;
    semanal::AnalyzedPackage analyzed;
    refanal::ir::Module refanal_module;
};

class Project {
public:
    Project(const std::filesystem::path& input_path);

    void parse(bool show_ast);
    void semanal(bool show_semanal);
    void refanal(bool show_refanal);
    void codegen(
        bool show_llvm_ir,
        bool show_opt_llvm_ir,
        llvm::OptimizationLevel opt,
        const std::filesystem::path& output_path
    );
    void run_jit(
        bool show_llvm_ir, bool show_opt_llvm_ir, llvm::OptimizationLevel opt
    );

private:
    ErrorHandler err_handler_;
    std::vector<std::unique_ptr<Package>> packages_;
};
}