#pragma once

#include "ir.h"
#include "project.h"

namespace acu::semanal {
struct UsedModule {
    ir::PackageRef package;
    const ir::Module* module;
};

class Context {
public:
    Context(const Source& source) : source_(&source) { push(); }

    struct ScopeEntry {
        utils::Variant<
            ir::InstRef,
            ir::ParamRef,
            ir::FuncRef,
            types::TypeId,
            std::string_view,
            UsedModule,
            ir::UsedFuncRef>
            data;
        Location location;
    };

    [[nodiscard]] const Source& source() const { return *source_; }

    void push() { scopes_stack_.emplace_back(); }

    void pop() { scopes_stack_.pop_back(); }

    [[nodiscard]] const ScopeEntry* find(std::string_view name) const;

    /// @return Pointer to existing entry if redefinition, nullptr otherwise.
    const ScopeEntry* add(std::string_view name, ScopeEntry entry) {
        auto& current_scope = scopes_stack_.back();
        if (auto it = current_scope.find(name); it != current_scope.end()) {
            return &it->second;
        }
        current_scope[name] = entry;
        return nullptr;
    }

    [[nodiscard]] std::vector<std::string_view> get_all_names() const;

    std::string suggest_similar_name(std::string_view name);

private:
    const Source* source_;
    std::vector<std::unordered_map<std::string_view, ScopeEntry>> scopes_stack_;
};

class Resolver {
public:
    Resolver(
        PackageName package_name,
        std::span<const ModuleRef> modules,
        const Packages& project_context,
        ErrorHandler& err_handler
    );

    ir::Package resolve();

private:
    void set_context(PackageNameRef module_name);
    utils::Variant<Context*, UsedModule> get_module_context(
        PackageNameRef module_name, Location location
    );

    bool flatten_module_path(
        const nodes::Expr& expr, PackageName& path
    );

    std::optional<Context::ScopeEntry> find_in_module_item(
        const utils::Variant<Context*, UsedModule>& module,
        std::string_view item_name
    );

    std::optional<Context::ScopeEntry> find_in_imported_module_chain(
        PackageNameRef path
    );

    struct ImportedModule {
        PackageNameRef path;
        utils::Variant<Context*, UsedModule> module;
    };

    void resolve_using(const nodes::Use& use, Location location);

    void resolve_using(const nodes::FromUse& use, Location location);

    types::TypeId create_struct_def(
        const nodes::Struct& struct_node, Location location
    );

    void resolve_struct_def(const nodes::Struct& struct_node);

    ir::FuncRef create_func_def(
        const nodes::Func& func_node, Location location
    );

    void resolve_func_def(const nodes::Func& func_node);

    ir::UsedFuncRef get_used_func(ir::PackageRef package_ref, ir::FuncRef ref);

    types::SpecType resolve_type(const nodes::Expr& expr);

    std::int64_t get_int_const(const nodes::Expr& expr);

    void resolve_stmt(const nodes::Stmt& stmt, ir::Func& func);

    ir::Block resolve_block(ir::Func& func, std::invocable auto&& resolve);
    ir::Block resolve_block(const nodes::Stmt& stmt, ir::Func& func);
    ir::Block resolve_block(const nodes::Expr& expr, ir::Func& func);

    ir::InstRef convert_store(
        ir::InstRef value, ir::Func& func, const nodes::Expr& expr
    );

    ir::Inst::Const convert_const(const nodes::Expr::Literal& lit);

    ir::InstRef resolve_expr(const nodes::Expr& expr, ir::Func& func);

    std::span<const ModuleRef> modules_;
    ErrorHandler* err_handler_;
    ir::Package ir_package_;
    std::optional<Context> root_context_;
    std::unordered_map<std::string_view, Context> module_contexts_;
    std::vector<ImportedModule> imported_modules_;

    struct UsedFuncHash {
        std::size_t operator()(ir::UsedFunc func) const {
            std::size_t result = 0;
            hash_combine(result, func.package.index);
            hash_combine(result, func.func.index);
            return result;
        }
    };

    struct UsedFuncEqual {
        bool operator()(ir::UsedFunc func1, ir::UsedFunc func2) const {
            return func1.package.index == func2.package.index &&
                   func1.func.index == func2.func.index;
        }
    };

    std::unordered_map<
        ir::UsedFunc,
        ir::UsedFuncRef,
        UsedFuncHash,
        UsedFuncEqual>
        used_funcs_;
    Context* context_ = nullptr;
    const Packages* project_context_;
};
}
