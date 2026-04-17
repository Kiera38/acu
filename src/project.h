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
    std::unique_ptr<nodes::Module> module;
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

    [[nodiscard]] const ir::Package& package(ir::PackageRef ref) const {
        return packages_[PackageRef {ref.index}].ir_package;
    }
    [[nodiscard]] ir::Package& package(ir::PackageRef ref) {
        return packages_[PackageRef {ref.index}].ir_package;
    }
    [[nodiscard]] const refanal::ir::Module& module(
        refanal::ir::ModuleRef ref
    ) const {
        return packages_[PackageRef {ref.index}].refanal_module;
    }

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

template <class T>
static void hash_combine(std::size_t& seed, const T& v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct PackageNameHash {
    std::size_t operator()(std::span<const std::string_view> name) const {
        std::size_t result {};
        for (auto i : name) {
            hash_combine(result, i);
        }
        return result;
    }
};
struct PackageNameEqual {
    bool operator()(
        std::span<const std::string_view> name1,
        std::span<const std::string_view> name2
    ) const {
        if (name1.size() != name2.size()) return false;
        for (const auto& [i1, i2] : std::views::zip(name1, name2)) {
            if (i1 != i2) return false;
        }
        return true;
    }
};

class Packages {
public:
    Packages(Project& project);
    std::vector<PackageRef> sort();
    [[nodiscard]] const nodes::Module& module(ModuleRef ref) const {
        return *project_->modules_[ref].module;
    }
    [[nodiscard]] std::span<const std::string_view> package_name(
        std::span<const std::string_view> module_name
    ) const;
    [[nodiscard]] const ir::Package& package(
        std::span<const std::string_view> name
    ) const {
        return project_->packages_[packages_.at(name)].ir_package;
    }
    [[nodiscard]] ir::Package& package(ir::PackageRef ref) const {
        return project_->package(ref);
    }
    [[nodiscard]] std::pair<ir::PackageRef, ir::Module*> module_package(
        std::span<const std::string_view> module_name
    ) const;

private:
    std::unordered_map<
        std::span<const std::string_view>,
        PackageRef,
        PackageNameHash,
        PackageNameEqual>
        packages_;
    std::unordered_map<
        std::span<const std::string_view>,
        ModuleRef,
        PackageNameHash,
        PackageNameEqual>
        modules_;
    IndexVector<
        std::unordered_set<PackageRef, hash<PackageRef>, equal_to<PackageRef>>,
        PackageRef>
        package_usings_;
    Project* project_;
};

}