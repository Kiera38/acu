#pragma once
#include <filesystem>
#include <string>

#include "errors.h"
#include "hash.h"
#include "index.h"
#include "package_name.h"
#include "parser/nodes.h"
#include "refanal/ir.h"
#include "semanal/ir.h"
#include "source.h"

namespace acu {

struct Module {
    Source source;
    std::unique_ptr<nodes::Module> module;
};

using ModuleRef = Ref<Module>;
struct Package {
    std::string package_name;
    std::vector<ModuleRef> modules;
    ir::Package ir_package;
    ir::AnalyzedPackage analyzed;
    refanal::ir::Module refanal_module;
    std::optional<PackageName> _name;

    [[nodiscard]] PackageNameRef name() {
        if (!_name) {
            _name.emplace(package_name);
        }
        return *_name;
    }
};

using PackageRef = Ref<Package>;

enum class OptimizationLevel : std::uint8_t { O0, O1, O2, O3, Os, Oz };

class Project {
public:
    Project(const std::filesystem::path& input_path);

    [[nodiscard]] const ir::Package& package(ir::PackageRef ref) const {
        return packages_[PackageRef {ref.index}].ir_package;
    }
    [[nodiscard]] ir::Package& package(ir::PackageRef ref) {
        return packages_[PackageRef {ref.index}].ir_package;
    }
    [[nodiscard]] const ir::AnalyzedPackage& apackage(ir::PackageRef ref) const {
        return packages_[PackageRef {ref.index}].analyzed;
    }
    [[nodiscard]] ir::AnalyzedPackage& apackage(ir::PackageRef ref) {
        return packages_[PackageRef {ref.index}].analyzed;
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
        OptimizationLevel opt,
        const std::filesystem::path& output_path
    );
    void run_jit(
        bool show_llvm_ir, bool show_opt_llvm_ir, OptimizationLevel opt
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
    [[nodiscard]] nodes::Module& module(ModuleRef ref) const {
        return *project_->modules_[ref].module;
    }
    [[nodiscard]] const ir::Package& package(PackageNameRef name) const {
        return project_->packages_[packages_.at(name)].ir_package;
    }
    [[nodiscard]] ir::Package& package(ir::PackageRef ref) const {
        return project_->package(ref);
    }
    [[nodiscard]] ir::AnalyzedPackage& apackage(ir::PackageRef ref) const {
        return project_->apackage(ref);
    }
    [[nodiscard]] std::pair<ir::PackageRef, ir::Module*> module_package(
        PackageNameRef module_name
    ) const;

private:
    std::unordered_map<PackageNameRef, PackageRef, hash<PackageNameRef>>
        packages_;
    std::unordered_map<PackageNameRef, ModuleRef, hash<PackageNameRef>>
        modules_;
    IndexVector<
        std::unordered_set<PackageRef, hash<PackageRef>, equal_to<PackageRef>>,
        PackageRef>
        package_usings_;
    Project* project_;
};

}