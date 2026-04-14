#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "parser/nodes.h"
#include "semanal.h"
#include "semanal/ir.h"
#include "semanal/semanal.h"
#include "semanal/types.h"
#include "source.h"

namespace acu::semanal {
namespace {

std::size_t levenshtein_distance(std::string_view s1, std::string_view s2) {
    if (s1.empty()) return s2.size();
    if (s2.empty()) return s1.size();

    std::vector<std::uint32_t> v0(s2.size() + 1);
    std::vector<std::uint32_t> v1(s2.size() + 1);

    for (std::uint32_t i = 0; i <= s2.size(); i++) v0[i] = i;

    for (std::uint32_t i = 0; i < s1.size(); i++) {
        v1[0] = i + 1;
        for (std::uint32_t j = 0; j < s2.size(); j++) {
            std::uint32_t cost = (s1[i] == s2[j]) ? 0 : 1;
            v1[j + 1] = std::min({v1[j] + 1, v0[j + 1] + 1, v0[j] + cost});
        }
        std::swap(v0, v1);
    }
    return v0[s2.size()];
}

struct UsedModule {
    const ir::Package* package;
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
    };

    [[nodiscard]] const Source& source() const { return *source_; }

    void push() { scopes_stack_.emplace_back(); }

    void pop() { scopes_stack_.pop_back(); }

    [[nodiscard]] const ScopeEntry* find(std::string_view name) const {
        for (const auto& it : std::ranges::reverse_view(scopes_stack_)) {
            auto entry_it = it.find(name);
            if (entry_it != it.end()) {
                return &entry_it->second;
            }
        }
        return nullptr;
    }

    void add(std::string_view name, ScopeEntry entry) {
        scopes_stack_.back()[name] = entry;
    }

    [[nodiscard]] std::vector<std::string_view> get_all_names() const {
        std::vector<std::string_view> names;
        for (const auto& scope : scopes_stack_) {
            for (const auto& [name, _] : scope) {
                names.push_back(name);
            }
        }
        return names;
    }

    std::string suggest_similar_name(std::string_view name) {
        auto names = get_all_names();
        static constexpr std::array<std::string_view, 18> builtins = {
            "Int",
            "Int8",
            "Int16",
            "Int32",
            "Int64",
            "UInt",
            "UInt8",
            "UInt16",
            "UInt32",
            "UInt64",
            "Float",
            "Float32",
            "Float64",
            "Bool",
            "None",
            "Nothing",
            "Array",
            "Ptr"
        };
        for (auto b : builtins) {
            names.push_back(b);
        }

        std::string_view best_match;
        std::size_t min_distance = (name.length() < 3) ? 1 : 3;

        for (auto n : names) {
            if (n == name) continue;
            auto dist = levenshtein_distance(name, n);
            if (dist < min_distance) {
                min_distance = dist;
                best_match = n;
            }
        }

        if (!best_match.empty()) {
            return std::format("did you mean '{}'?", best_match);
        }
        return "";
    }

private:
    const Source* source_;
    std::vector<std::unordered_map<std::string_view, ScopeEntry>> scopes_stack_;
};

std::uint8_t as_uint8(
    std::string_view str,
    const Source& source,
    Location loc,
    ErrorHandler& err_handler
) {
    std::uint8_t result = 0;
    auto [ptr, err] =
        std::from_chars(str.data(), str.data() + str.length(), result);
    if (err != std::errc {}) {
        err_handler.error(source, loc, "error in number");
    }
    return result;
}

std::string_view get_relative_module_name(std::string_view full_name) {
    if (auto pos = full_name.find_last_of('.'); pos != std::string_view::npos) {
        return full_name.substr(pos + 1);
    }
    return full_name;
}

class Resolver {
public:
    Resolver(
        std::vector<std::string_view> package_name,
        std::span<const nodes::Module> modules,
        const ProjectContext& project_context,
        ErrorHandler& err_handler
    )
        : ir_package_(std::move(package_name)),
          modules_(modules),
          project_context_(&project_context),
          err_handler_(&err_handler) {}

    ir::Package resolve() {
        for (const auto& mod : modules_) {
            auto module_name =
                get_relative_module_name(mod.source->module_name);
            module_contexts_.insert({module_name, Context(*mod.source)});
            context_ = &module_contexts_.at(module_name);
            auto& module = ir_package_.add_module(module_name);
            for (const auto& item : mod.items) {
                item.data.visit(
                    [&](const nodes::Func& func) {
                        auto func_ref = create_func_def(func, item.location);
                        if (func.is_public) {
                            module.add_func(func.name, func_ref);
                        }
                    },
                    [&](const nodes::Struct& struct_def) {
                        auto struct_type =
                            create_struct_def(struct_def, item.location);
                        module.add_struct(struct_def.name, struct_type);
                    },
                    [&](const auto&) {}
                );
            }
        }

        for (const auto& mod : modules_) {
            context_ = &module_contexts_.at(
                get_relative_module_name(mod.source->module_name)
            );

            for (const auto& item : mod.items) {
                item.data.visit(
                    [&](const nodes::Use& use) {
                        resolve_using(use, item.location);
                    },
                    [&](const nodes::FromUse& use) {
                        resolve_using(use, item.location);
                    },
                    [&](const nodes::Func& func) { resolve_func_def(func); },
                    [&](const nodes::Struct& struct_def) {
                        resolve_struct_def(struct_def);
                    }
                );
            }
        }

        return std::move(ir_package_);
    }

private:
    utils::Variant<Context*, UsedModule> get_module_context(
        std::span<const std::string_view> module_name, Location location
    ) {
        if (module_name.size() < ir_package_.name().size()) {
            if (auto package = project_context_->get_package(
                    module_name.subspan(0, module_name.size() - 1)
                )) {
                return UsedModule {
                    .package = package,
                    .module = &package->module(module_name.back())
                };
            } else {
                err_handler_->error(
                    context_->source(),
                    location,
                    std::format(
                        "module {} not found", join_module_name(module_name)
                    )
                );
                return static_cast<Context*>(nullptr);
            }
        }
        for (const auto& [package_name, using_name] :
             std::views::zip(ir_package_.name(), module_name)) {
            if (package_name != using_name) {
                if (auto package = project_context_->get_package(
                        module_name.subspan(0, module_name.size() - 1)
                    )) {
                    return UsedModule {
                        .package = package,
                        .module = &package->module(module_name.back())
                    };
                } else {
                    err_handler_->error(
                        context_->source(),
                        location,
                        std::format(
                            "module {} not found", join_module_name(module_name)
                        )
                    );
                    return static_cast<Context*>(nullptr);
                }
            }
        }
        auto name = std::span(module_name).subspan(ir_package_.name().size());
        if (name.empty()) {
            if (!ir_package_.name().empty()) {
                if (auto it = module_contexts_.find(ir_package_.name().back());
                    it != module_contexts_.end()) {
                    return &it->second;
                }
            }
        } else if (name.size() == 1) {
            if (auto it = module_contexts_.find(name[0]);
                it != module_contexts_.end()) {
                return &it->second;
            }
        }
        err_handler_->error(
            context_->source(),
            location,
            std::format("module '{}' not found", join_module_name(module_name))
        );
        return static_cast<Context*>(nullptr);
    }

    std::string join_module_name(
        std::span<const std::string_view> module_name
    ) {
        return module_name | std::views::join_with('.') |
               std::ranges::to<std::string>();
    }

    void resolve_using(const nodes::Use& use, Location location) {
        auto module = get_module_context(use.module_name, location);
        module.visit(
            [&](Context* module_context) {
                if (module_context) {
                    context_->add(
                        use.module_name.back(), {use.module_name.back()}
                    );
                }
            },
            [&](UsedModule module) {
                context_->add(use.module_name.back(), {module});
            }
        );
    }

    void resolve_using(const nodes::FromUse& use, Location location) {
        auto module = get_module_context(use.module_name, location);
        module.visit(
            [&](Context* module_context) {
                if (module_context) {
                    for (const auto& item : use.items) {
                        if (auto entry = module_context->find(item.name)) {
                            context_->add(
                                item.alias.value_or(item.name), *entry
                            );
                        } else {
                            err_handler_->error(
                                context_->source(),
                                item.location,
                                std::format(
                                    "name '{}' not found in module '{}'",
                                    item.name,
                                    join_module_name(use.module_name)
                                )
                            );
                        }
                    }
                }
            },
            [&](UsedModule module) {
                for (const auto& item : use.items) {
                    module.module->find(item.name).visit(
                        [&](std::monostate) {},
                        [&](ir::FuncRef ref) {
                            context_->add(
                                item.alias.value_or(item.name),
                                {get_used_func(*module.package, ref)}
                            );
                        },
                        [&](types::TypeId type) {
                            context_->add(
                                item.alias.value_or(item.name),
                                {ir_package_.types().add_used_struct(
                                    types::Type::UsedStruct {
                                        .pool = &module.package->types(),
                                        .type = type
                                    }
                                )}
                            );
                        }
                    );
                }
            }
        );
    }

    types::TypeId create_struct_def(
        const nodes::Struct& struct_node, Location location
    ) {
        auto type_id = ir_package_.types().add_struct({
            .name = struct_node.name,
            .source = &context_->source(),
            .location = location,
        });
        context_->add(struct_node.name, {type_id});
        return type_id;
    }

    void resolve_struct_def(const nodes::Struct& struct_node) {
        auto type_id_it = context_->find(struct_node.name);
        if (type_id_it != nullptr) {
            auto type_id = type_id_it->data.get<types::TypeId>();
            std::vector<types::Type::StructField> fields;
            fields.reserve(struct_node.fields.size());
            for (const auto& field : struct_node.fields) {
                auto field_type = resolve_type(*field.type);
                if (field_type.specifier == types::Specifier::None) {
                    field_type.specifier = types::Specifier::Val;
                }
                fields.push_back({.name = field.name, .type = field_type});
            }
            ir_package_.types().set_struct_fields(type_id, std::move(fields));
        }
    }

    ir::FuncRef create_func_def(
        const nodes::Func& func_node, Location location
    ) {
        ir::Func ir_func(
            func_node.name, context_->source(), location, func_node.is_extern
        );
        auto func_ref = ir_package_.add(std::move(ir_func));
        context_->add(func_node.name, {func_ref});
        return func_ref;
    }

    void resolve_func_def(const nodes::Func& func_node) {
        auto* entry = context_->find(func_node.name);
        if (entry) {
            ir::Func& ir_func =
                ir_package_.func(entry->data.get<ir::FuncRef>());
            std::vector<ir::Param> ir_params;
            ir_params.reserve(func_node.args.size());
            for (const auto& arg : func_node.args) {
                auto param_type = resolve_type(*arg.type);
                if (func_node.is_extern) {
                    if (param_type.specifier != types::Specifier::None &&
                        param_type.specifier != types::Specifier::Val) {
                        err_handler_->error(
                            context_->source(),
                            arg.location,
                            "in extern function all parameters must have 'val' "
                            "specifier"
                        );
                    }
                    param_type.specifier = types::Specifier::Val;
                } else if (param_type.specifier == types::Specifier::None) {
                    param_type.specifier = types::Specifier::Let;
                }
                ir_params.push_back({.name = arg.name, .type = param_type});
            }
            types::SpecType return_type = {.type = types::None};
            if (func_node.return_type) {
                return_type = resolve_type(*func_node.return_type);
                if (func_node.is_extern) {
                    if (return_type.specifier != types::Specifier::None &&
                        return_type.specifier != types::Specifier::Val) {
                        err_handler_->error(
                            context_->source(),
                            func_node.return_type->location,
                            "in extern function return type must have val "
                            "specifier"
                        );
                    }
                    return_type.specifier = types::Specifier::Val;
                } else if (return_type.specifier == types::Specifier::None) {
                    return_type.specifier = types::Specifier::Val;
                }
            }
            ir_func.set_type(ir_params, return_type);

            if (!func_node.is_extern && func_node.body) {
                context_->push();
                for (size_t i = 0; i < ir_params.size(); ++i) {
                    context_->add(
                        func_node.args[i].name,
                        {ir::ParamRef {static_cast<std::uint32_t>(i)}}
                    );
                }
                resolve_stmt(*func_node.body, ir_func);
                context_->pop();
            }
        }
    }

    ir::UsedFuncRef get_used_func(const ir::Package& package, ir::FuncRef ref) {
        ir::UsedFunc used_func {.package = &package, .func = ref};
        if(auto it = used_funcs_.find(used_func); it != used_funcs_.end()) {
            return it->second;
        }
        auto used_func_ref = ir_package_.add(used_func);
        used_funcs_.insert({used_func, used_func_ref});
        return used_func_ref;
    }

    std::optional<Context::ScopeEntry> find_in_module(
        std::string_view module_name, std::string_view item_name
    ) {
        if (auto entry = context_->find(module_name)) {
            if (auto* mod_name_ptr = entry->data.get_if<std::string_view>()) {
                auto mod_name = *mod_name_ptr;
                if (auto it = module_contexts_.find(mod_name);
                    it != module_contexts_.end()) {
                    if (auto item_entry = it->second.find(item_name)) {
                        return *item_entry;
                    }
                }
            } else if (auto module = entry->data.get_if<UsedModule>()) {
                return module->module->find(item_name).visit(
                    [&](std::monostate) -> std::optional<Context::ScopeEntry> {
                        return std::nullopt;
                    },
                    [&](ir::FuncRef ref) -> std::optional<Context::ScopeEntry> {
                        return Context::ScopeEntry {
                            get_used_func(*module->package, ref)
                        };
                    },
                    [&](types::TypeId type)
                        -> std::optional<Context::ScopeEntry> {
                        return Context::ScopeEntry {
                            ir_package_.types().add_used_struct(
                                types::Type::UsedStruct {
                                    .pool = &module->package->types(),
                                    .type = type
                                }
                            )
                        };
                    }
                );
            }
        }
        return std::nullopt;
    }

    types::SpecType resolve_type(const nodes::Expr& expr) {
        return expr.value.visit(
            [&](const nodes::Expr::Name& name) -> types::SpecType {
                if (auto type = context_->find(name.name)) {
                    return {.type = type->data.get<types::TypeId>()};
                } else {
                    if (name.name.starts_with("Int")) {
                        if (name.name == "Int") return {.type = types::Int};
                        auto bits = as_uint8(
                            name.name.substr(3),
                            context_->source(),
                            expr.location,
                            *err_handler_
                        );
                        switch (bits) {
                            case 8: return {.type = types::Int8};
                            case 16: return {.type = types::Int16};
                            case 32: return {.type = types::Int32};
                            case 64: return {.type = types::Int64};
                            default:
                                err_handler_->error(
                                    context_->source(),
                                    expr.location,
                                    "unsupported integer size"
                                );
                                return {.type = types::None};
                        }
                    } else if (name.name.starts_with("UInt")) {
                        if (name.name == "UInt") return {.type = types::UInt};
                        auto bits = as_uint8(
                            name.name.substr(4),
                            context_->source(),
                            expr.location,
                            *err_handler_
                        );
                        switch (bits) {
                            case 8: return {.type = types::UInt8};
                            case 16: return {.type = types::UInt16};
                            case 32: return {.type = types::UInt32};
                            case 64: return {.type = types::UInt64};
                            default:
                                err_handler_->error(
                                    context_->source(),
                                    expr.location,
                                    "unsupported integer size"
                                );
                                return {.type = types::None};
                        }
                    } else if (name.name.starts_with("Float")) {
                        if (name.name == "Float") return {.type = types::Float};
                        auto bits = as_uint8(
                            name.name.substr(5),
                            context_->source(),
                            expr.location,
                            *err_handler_
                        );
                        switch (bits) {
                            case 32: return {.type = types::Float32};
                            case 64: return {.type = types::Float64};
                            default:
                                err_handler_->error(
                                    context_->source(),
                                    expr.location,
                                    "unsupported float size"
                                );
                                return {.type = types::None};
                        }
                    } else if (name.name == "Bool") {
                        return {.type = types::Bool};
                    } else if (name.name == "None") {
                        return {.type = types::None};
                    } else if (name.name == "Nothing") {
                        return {.type = types::Nothing};
                    }
                    err_handler_->error(
                        context_->source(),
                        expr.location,
                        std::format("name '{}' not found", name.name),
                        context_->suggest_similar_name(name.name)
                    );
                    return {.type = types::None};
                }
            },
            [&](const nodes::Expr::GetItem& node) -> types::SpecType {
                const auto& name = node.value->value.get<nodes::Expr::Name>();
                if (name.name == "Array") {
                    if (node.args.size() != 2) {
                        err_handler_->error(
                            context_->source(),
                            expr.location,
                            "Array type has 2 parameters"
                        );
                        return {.type = types::None};
                    }
                    auto type = resolve_type(*node.args[0]);
                    if (type.specifier == types::Specifier::None) {
                        type.specifier = types::Specifier::Val;
                    }
                    auto length = get_int_const(*node.args[1]);
                    return {
                        .type = ir_package_.types().add_array(type, length)
                    };
                } else if (name.name == "Ptr") {
                    if (node.args.size() != 1) {
                        err_handler_->error(
                            context_->source(),
                            expr.location,
                            "Ptr type has 1 parameter"
                        );
                        return {.type = types::None};
                    }
                    auto type = resolve_type(*node.args[0]);
                    if (type.specifier == types::Specifier::None) {
                        type.specifier = types::Specifier::Val;
                    }
                    return {.type = ir_package_.types().add_ptr(type)};
                } else {
                    err_handler_->error(
                        context_->source(), expr.location, "unknown type"
                    );
                    return {.type = types::None};
                }
            },
            [&](const nodes::Expr::Spec& spec) -> types::SpecType {
                auto specifier = [&] {
                    switch (spec.specifier) {
                        using enum nodes::Expr::Specifier;
                        case Let: return types::Specifier::Let;
                        case Var: return types::Specifier::Var;
                        case Val: return types::Specifier::Val;
                    }
                    std::unreachable();
                }();
                return {
                    .type = resolve_type(*spec.type).type,
                    .specifier = specifier
                };
            },
            [&](const nodes::Expr::GetAttr& node) -> types::SpecType {
                if (auto* name_node =
                        node.value->value.get_if<nodes::Expr::Name>()) {
                    if (auto item_entry =
                            find_in_module(name_node->name, node.name)) {
                        if (auto* type_id_ptr =
                                item_entry->data.get_if<types::TypeId>()) {
                            return {.type = *type_id_ptr};
                        }
                    }
                }
                err_handler_->error(
                    context_->source(), expr.location, "expr is not a type"
                );
                return {.type = types::None};
            },
            [&](const auto&) -> types::SpecType {
                err_handler_->error(
                    context_->source(), expr.location, "expr is not a type"
                );
                return {.type = types::None};
            }
        );
    }

    std::int64_t get_int_const(const nodes::Expr& expr) {
        if (auto* lit = expr.value.get_if<nodes::Expr::Literal>()) {
            if (auto* val = lit->value.get_if<std::int64_t>()) {
                return *val;
            }
        }
        err_handler_->error(
            context_->source(), expr.location, "Expected integer constant"
        );
        return 0;
    }

    void resolve_stmt(const nodes::Stmt& stmt, ir::Func& func) {
        stmt.value.visit(
            [&](const nodes::Stmt::Expr& data) {
                resolve_expr(*data.expr, func);
            },
            [&](const nodes::Stmt::Var& data) {
                std::optional<types::SpecType> type;
                if (data.type) {
                    type = resolve_type(*data.type);
                }
                auto var_ref = func.add({
                    .data = ir::Inst::VarDecl {.name = data.name, .type = type},
                    .location = stmt.location,
                });

                context_->add(data.name, {var_ref});
                if (data.init) {
                    auto value_ref = resolve_expr(*data.init, func);
                    func.add({
                        .data =
                            ir::Inst::Store {
                                .var = var_ref, .value = value_ref
                            },
                        .location = stmt.location,
                    });
                }
            },
            [&](const nodes::Stmt::Block& data) {
                context_->push();
                for (const auto& stmt : data.stmts) {
                    resolve_stmt(*stmt, func);
                }
                context_->pop();
            },
            [&](const nodes::Stmt::If& data) {
                auto cond_ref = resolve_expr(*data.cond, func);
                auto if_ref = func.add({
                    .data = ir::Inst::If {.value = cond_ref},
                    .location = stmt.location,
                });

                ir::Block then_block = resolve_block(*data.then_block, func);
                std::optional<ir::Block> else_block {};
                if (data.else_block) {
                    else_block = resolve_block(*data.else_block, func);
                }
                func.set_if_blocks(if_ref, then_block, else_block);
            },
            [&](const nodes::Stmt::While& data) {
                auto loop_ref = func.add({
                    .data = ir::Inst::Loop {},
                    .location = stmt.location,
                });
                auto loop_block = resolve_block(func, [&] {
                    auto cond_ref = resolve_expr(*data.cond, func);
                    auto if_ref = func.add({
                        .data = ir::Inst::If {.value = cond_ref},
                        .location = stmt.location,
                    });
                    auto then_block = resolve_block(*data.body, func);
                    auto break_ref = func.add({
                        .data = ir::Inst::Break {},
                        .location = stmt.location,
                    });
                    func.set_if_blocks(
                        if_ref,
                        then_block,
                        ir::Block {.start = break_ref, .end = break_ref}
                    );
                });
                func.set_loop_block(loop_ref, loop_block);
            },
            [&](const nodes::Stmt::Return& data) {
                ir::Inst return_inst;
                if (data.value) {
                    auto value_ref = resolve_expr(*data.value, func);
                    return_inst.data = ir::Inst::Return {.value = value_ref};
                } else {
                    return_inst.data = ir::Inst::Return {.value = std::nullopt};
                }
                return_inst.location = stmt.location;

                func.add(return_inst);
            },
            [&](const nodes::Stmt::Break& data) {
                func.add({
                    .data = ir::Inst::Break {},
                    .location = stmt.location,
                });
            },
            [&](const nodes::Stmt::Continue& data) {
                func.add({
                    .data = ir::Inst::Continue {},
                    .location = stmt.location,
                });
            },
            [&](const nodes::Stmt::Assign& data) {
                if (!data.targets.empty()) {
                    auto value_ref = resolve_expr(*data.value, func);
                    for (const auto& target : data.targets) {
                        convert_store(value_ref, func, *target);
                    }
                }
            },
            [&](const nodes::Stmt::OpAssign& data) {
                auto target_ref = resolve_expr(*data.target, func);
                auto value_ref = resolve_expr(*data.value, func);
                auto ir_op = [&] {
                    switch (data.op) {
                        using enum nodes::Stmt::AssignOp;
                        case Add: return ir::Inst::BinaryOp::Add;
                        case Sub: return ir::Inst::BinaryOp::Sub;
                        case Mul: return ir::Inst::BinaryOp::Mul;
                        case Div: return ir::Inst::BinaryOp::Div;
                        case Mod: return ir::Inst::BinaryOp::Mod;
                        case LShift: return ir::Inst::BinaryOp::LShift;
                        case RShift: return ir::Inst::BinaryOp::RShift;
                        case BitAnd: return ir::Inst::BinaryOp::BitAnd;
                        case BitOr: return ir::Inst::BinaryOp::BitOr;
                        case BitXor: return ir::Inst::BinaryOp::BitXor;
                    }
                    std::unreachable();
                }();
                convert_store(
                    func.add({
                        .data =
                            ir::Inst::Binary {
                                .left = target_ref,
                                .right = value_ref,
                                .op = ir_op
                            },
                        .location = stmt.location,
                    }),
                    func,
                    *data.target
                );
            },
            [&](const auto&) {}
        );
    }

    ir::Block resolve_block(ir::Func& func, std::invocable auto&& resolve) {
        auto start_inst = func.last_inst();
        std::invoke(resolve);
        return {.start = {start_inst.index + 1}, .end = func.last_inst()};
    }

    ir::Block resolve_block(const nodes::Stmt& stmt, ir::Func& func) {
        return resolve_block(func, [&] { resolve_stmt(stmt, func); });
    }

    ir::Block resolve_block(const nodes::Expr& expr, ir::Func& func) {
        return resolve_block(func, [&] { resolve_expr(expr, func); });
    }

    ir::InstRef convert_store(
        ir::InstRef value, ir::Func& func, const nodes::Expr& expr
    ) {
        return expr.value.visit(
            [&](const nodes::Expr::Name& node) {
                if (auto var = context_->find(node.name)) {
                    auto ref = var->data.get<ir::InstRef>();
                    return func.add({
                        .data = ir::Inst::Store {.var = ref, .value = value},
                        .location = expr.location,
                    });
                } else {
                    auto var_ref = func.add({
                        .data = ir::Inst::VarDecl {.name = node.name},
                        .location = expr.location,
                    });
                    context_->add(node.name, {var_ref});
                    return func.add({
                        .data =
                            ir::Inst::Store {.var = var_ref, .value = value},
                        .location = expr.location,
                    });
                }
            },
            [&](const nodes::Expr::GetItem& node) {
                return func.add({
                    .data =
                        ir::Inst::SetItem {
                            .var = resolve_expr(*node.value, func),
                            .index = resolve_expr(*node.args[0], func),
                            .value = value
                        },
                    .location = expr.location,
                });
            },
            [&](const nodes::Expr::GetAttr& node) {
                return func.add({
                    .data =
                        ir::Inst::SetAttr {
                            .var = resolve_expr(*node.value, func),
                            .value = value,
                            .name = node.name,
                        },
                    .location = expr.location,
                });
            },
            [&](const auto&) -> ir::InstRef {
                err_handler_->error(
                    context_->source(),
                    expr.location,
                    "cannot use this expression on the left "
                    "side of assignment"
                );
                return value;
            }
        );
    }

    ir::Inst::Const convert_const(const nodes::Expr::Literal& lit) {
        return lit.value.visit([&](auto v) -> ir::Inst::Const { return {v}; });
    }

    ir::InstRef resolve_expr(const nodes::Expr& expr, ir::Func& func) {
        return expr.value.visit(
            [&](const nodes::Expr::Literal& node) {
                return func.add({
                    .data = convert_const(node),
                    .location = expr.location,
                });
            },
            [&](const nodes::Expr::Name& node) {
                auto entry = context_->find(node.name);
                if (entry != nullptr) {
                    return func.add(entry->data.visit(
                        [&](ir::InstRef var_ref) -> ir::Inst {
                            return {
                                .data = ir::Inst::LoadVar {.var = var_ref},
                                .location = expr.location
                            };
                        },
                        [&](ir::ParamRef param_ref) -> ir::Inst {
                            return {
                                .data =
                                    ir::Inst::LoadParam {.param = param_ref},
                                .location = expr.location
                            };
                        },
                        [&](ir::FuncRef func_ref) -> ir::Inst {
                            return {
                                .data = ir::Inst::Const {.value = func_ref},
                                .location = expr.location
                            };
                        },
                        [&](ir::UsedFuncRef func_ref) -> ir::Inst {
                            return {
                                .data = ir::Inst::Const {.value = func_ref},
                                .location = expr.location
                            };
                        },
                        [&](types::TypeId struct_ref) -> ir::Inst {
                            return {
                                .data = ir::Inst::Const {.value = struct_ref},
                                .location = expr.location
                            };
                        },
                        [&](const auto&) -> ir::Inst {
                            return {
                                .data = ir::Inst::Const {false},
                                .location = expr.location
                            };
                        }
                    ));
                } else {
                    err_handler_->error(
                        context_->source(),
                        expr.location,
                        std::format("name '{}' not found", node.name),
                        context_->suggest_similar_name(node.name)
                    );
                    return func.add({
                        .data = ir::Inst::Const {false},
                        .location = expr.location,
                    });
                }
            },
            [&](const nodes::Expr::Binary& node) {
                auto left_ref = resolve_expr(*node.left, func);
                if (node.op == nodes::Expr::BinaryOp::LogicalAnd ||
                    node.op == nodes::Expr::BinaryOp::LogicalOr) {
                    auto logical_ref = func.add({
                        .data =
                            ir::Inst::Logical {
                                .left = left_ref,
                                .op =
                                    node.op == nodes::Expr::BinaryOp::LogicalOr
                                        ? ir::Inst::LogicalOp::Or
                                        : ir::Inst::LogicalOp::And
                            },
                        .location = expr.location,
                    });

                    auto right_block = resolve_block(*node.right, func);
                    func.set_logical_block(logical_ref, right_block);
                    return logical_ref;
                } else {
                    auto right_ref = resolve_expr(*node.right, func);
                    ir::Inst::BinaryOp ir_op = [&] {
                        switch (node.op) {
                            using enum nodes::Expr::BinaryOp;
                            case Add: return ir::Inst::BinaryOp::Add;
                            case Sub: return ir::Inst::BinaryOp::Sub;
                            case Mul: return ir::Inst::BinaryOp::Mul;
                            case Div: return ir::Inst::BinaryOp::Div;
                            case Mod: return ir::Inst::BinaryOp::Mod;
                            case LShift: return ir::Inst::BinaryOp::LShift;
                            case RShift: return ir::Inst::BinaryOp::RShift;
                            case BitAnd: return ir::Inst::BinaryOp::BitAnd;
                            case BitOr: return ir::Inst::BinaryOp::BitOr;
                            case BitXor: return ir::Inst::BinaryOp::BitXor;
                            default: std::unreachable();
                        }
                    }();
                    return func.add({
                        .data =
                            ir::Inst::Binary {
                                .left = left_ref,
                                .right = right_ref,
                                .op = ir_op
                            },
                        .location = expr.location,
                    });
                }
            },
            [&](const nodes::Expr::Unary& node) {
                auto operand_ref = resolve_expr(*node.operand, func);
                if (node.op == nodes::Expr::UnaryOp::Deref ||
                    node.op == nodes::Expr::UnaryOp::AddressOf) {
                    switch (node.op) {
                        case nodes::Expr::UnaryOp::Deref:
                            return func.add({
                                .data = ir::Inst::Deref {operand_ref},
                                .location = expr.location,
                            });
                        case nodes::Expr::UnaryOp::AddressOf:
                            return func.add({
                                .data = ir::Inst::AddressOf {operand_ref},
                                .location = expr.location,
                            });
                        default: return func.add(ir::Inst {});
                    }
                }

                ir::Inst::UnaryOp ir_op = [&] {
                    switch (node.op) {
                        using enum nodes::Expr::UnaryOp;
                        case Not: return ir::Inst::UnaryOp::Not;
                        case Neg: return ir::Inst::UnaryOp::Neg;
                        case BitNot: return ir::Inst::UnaryOp::BitNot;
                        default: std::unreachable();
                    }
                }();

                return func.add({
                    .data = ir::Inst::Unary {.value = operand_ref, .op = ir_op},
                    .location = expr.location,
                });
            },
            [&](const nodes::Expr::Call& node) {
                auto callee_ref = resolve_expr(*node.value, func);

                std::vector<ir::InstRef> arg_refs;
                arg_refs.reserve(node.args.size());
                for (const auto& arg : node.args) {
                    arg_refs.push_back(resolve_expr(*arg, func));
                }
                auto inst_refs = func.add(arg_refs);
                return func.add({
                    .data =
                        ir::Inst::Call {.value = callee_ref, .args = inst_refs},
                    .location = expr.location,
                });
            },
            [&](const nodes::Expr::GetItem& node) {
                auto container_ref = resolve_expr(*node.value, func);
                auto index_ref = resolve_expr(*node.args[0], func);
                return func.add({
                    .data =
                        ir::Inst::GetItem {
                            .value = container_ref, .index = index_ref
                        },
                    .location = expr.location,
                });
            },
            [&](const nodes::Expr::GetAttr& node) {
                if (auto* name_node =
                        node.value->value.get_if<nodes::Expr::Name>()) {
                    if (auto item_entry =
                            find_in_module(name_node->name, node.name)) {
                        return func.add(item_entry->data.visit(
                            [&](ir::FuncRef func_ref) -> ir::Inst {
                                return {
                                    .data = ir::Inst::Const {.value = func_ref},
                                    .location = expr.location
                                };
                            },
                            [&](ir::UsedFuncRef func_ref) -> ir::Inst {
                                return {
                                    .data = ir::Inst::Const {.value = func_ref},
                                    .location = expr.location
                                };
                            },
                            [&](types::TypeId struct_ref) -> ir::Inst {
                                return {
                                    .data =
                                        ir::Inst::Const {.value = struct_ref},
                                    .location = expr.location
                                };
                            },
                            [&](const auto&) -> ir::Inst {
                                throw std::runtime_error("");
                                return {
                                    .data = ir::Inst::Const {false},
                                    .location = expr.location
                                };
                            }
                        ));
                    }
                }
                auto obj_ref = resolve_expr(*node.value, func);
                return func.add({
                    .data =
                        ir::Inst::GetAttr {.value = obj_ref, .name = node.name},
                    .location = expr.location,
                });
            },
            [&](const nodes::Expr::Array& node) {
                std::vector<ir::InstRef> item_refs;
                item_refs.reserve(node.items.size());
                for (const auto& item : node.items) {
                    item_refs.push_back(resolve_expr(*item, func));
                }

                auto inst_refs = func.add(item_refs);
                return func.add({
                    .data = ir::Inst::Array {inst_refs},
                    .location = expr.location,
                });
            },
            [&](const nodes::Expr::As& node) {
                auto value_ref = resolve_expr(*node.value, func);
                auto type = resolve_type(*node.type);
                return func.add({
                    .data = ir::Inst::As {.value = value_ref, .type = type},
                    .location = expr.location,
                });
            },
            [&](const nodes::Expr::Comparison& node) {
                if (node.operands.size() < 2 || node.operators.size() < 1 ||
                    node.operands.size() != node.operators.size() + 1) {
                    err_handler_->error(
                        context_->source(),
                        expr.location,
                        "Invalid comparison expression"
                    );
                    return func.add(ir::Inst {});
                }

                auto left_ref = resolve_expr(*node.operands[0], func);
                auto comparison_ref = func.add({
                    .data = ir::Inst::Comparison {.left = left_ref},
                    .location = expr.location,
                });

                std::vector<ir::Comparator> comparators;
                comparators.reserve(node.operators.size());
                for (size_t i = 0; i < node.operators.size(); ++i) {
                    auto right_block =
                        resolve_block(*node.operands[i + 1], func);
                    ir::ComparisonOp ir_op = [&] {
                        switch (node.operators[i]) {
                            using enum nodes::Expr::ComparisonOp;
                            case Less: return ir::ComparisonOp::Less;
                            case Greater: return ir::ComparisonOp::Greater;
                            case LessEqual: return ir::ComparisonOp::LessEqual;
                            case GreaterEqual:
                                return ir::ComparisonOp::GreaterEqual;
                            case Equal: return ir::ComparisonOp::Equal;
                            case NotEqual: return ir::ComparisonOp::NotEqual;
                        }
                        std::unreachable();
                    }();
                    comparators.push_back({
                        .value = right_block,
                        .op = ir_op,
                    });
                }
                func.set_comparators(comparison_ref, comparators);
                return comparison_ref;
            },
            [&](const auto&) { return func.add(ir::Inst {}); }
        );
    }

    std::span<const nodes::Module> modules_;
    ErrorHandler* err_handler_;
    ir::Package ir_package_;
    std::unordered_map<std::string_view, Context> module_contexts_;

    template <class T>
    static void hash_combine(std::size_t& seed, const T& v) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    struct UsedFuncHash {
        std::size_t operator()(ir::UsedFunc func) {
            std::size_t result = 0;
            hash_combine(result, func.package);
            hash_combine(result, func.func.index);
            return result;
        }
    };

    struct UsedFuncEqual {
        bool operator()(ir::UsedFunc func1, ir::UsedFunc func2) {
            return func1.package == func2.package && func1.func.index == func2.func.index;
        }
    };

    std::unordered_map<ir::UsedFunc, ir::UsedFuncRef, UsedFuncHash, UsedFuncEqual> used_funcs_;
    Context* context_ = nullptr;
    const ProjectContext* project_context_;
};
}

ir::Package resolve(
    std::vector<std::string_view> package_name,
    std::span<const nodes::Module> modules,
    const ProjectContext& context,
    ErrorHandler& err_handler
) {
    Resolver resolver(std::move(package_name), modules, context, err_handler);
    return resolver.resolve();
}

std::unordered_set<std::span<const std::string_view>, PackageNameHash>
get_module_usings(const nodes::Module& module) {
    std::unordered_set<std::span<const std::string_view>, PackageNameHash>
        usings;
    for (const auto& item : module.items) {
        item.data.visit(
            [&](const nodes::Use& use) { usings.emplace(use.module_name); },
            [&](const nodes::FromUse& use) { usings.emplace(use.module_name); },
            [&](const auto&) {}
        );
    }
    return usings;
}

}

namespace acu::ir {
utils::Variant<std::monostate, FuncRef, types::TypeId> Module::find(
    std::string_view name
) const {
    if (auto it = public_funcs_.find(name); it != public_funcs_.end()) {
        return it->second;
    }
    if (auto it = structs_.find(name); it != structs_.end()) {
        return it->second;
    }
    return std::monostate {};
}
}