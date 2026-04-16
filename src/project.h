#pragma once
#include <llvm/Passes/OptimizationLevel.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "errors.h"
#include "index.h"
#include "parser/nodes.h"
#include "refanal/ir.h"
#include "semanal/ir.h"
#include "source.h"

namespace acu {

struct Module {
    Source source;
    nodes::Module module;
};

struct ModuleRef {
    std::uint32_t index;
};
struct Package {
    std::string package_name;
    std::vector<std::string_view> name;
    std::vector<ModuleRef> modules;
    ir::Package ir_package;
    ir::AnalyzedPackage analyzed;
    refanal::ir::Module refanal_module;
};

struct PackageRef {
    std::uint32_t index;
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
    friend class Packages;
    ErrorHandler err_handler_;
    IndexVector<Module, ModuleRef> modules_;
    IndexVector<Package, PackageRef> packages_;
    std::vector<PackageRef> sorted_packages_;
};

class Packages {
public:
    Packages(Project& project);
    std::vector<PackageRef> sort();
    [[nodiscard]] const ir::Package& package(
        std::span<const std::string_view> name
    ) const;
    [[nodiscard]] const nodes::Module& module(ModuleRef ref) const;
    [[nodiscard]] std::pair<ir::Package*, ir::Module*> module_package(
        std::span<const std::string_view> module_name
    ) const;

private:
    template <class T>
    static void hash_combine(std::size_t& seed, const T& v) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    struct PackageNameHash {
        std::size_t operator()(std::span<const std::string_view> name) {
            std::size_t result {};
            for (auto i : name) {
                hash_combine(result, i);
            }
            return result;
        }
    };
    std::unordered_map<
        std::span<const std::string_view>,
        PackageRef,
        PackageNameHash>
        packages_;
    std::unordered_map<
        std::span<const std::string_view>,
        ModuleRef,
        PackageNameHash>
        modules_;
    IndexVector<std::unordered_set<PackageRef, hash<PackageRef>>, PackageRef>
        package_usings_;
    Project* project_;
};

}