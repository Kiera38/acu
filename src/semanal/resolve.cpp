#include "resolve.h"

#include <stack>

namespace acu::semanal {
ModuleContext resolve(const nodes::Module& module, Project& project) {
    ModuleContext result;
    for (const auto& item : module.items) {
        item.visit(
            [&](const nodes::Use& use) {
                project.add_module(
                    use.module_name, *module.source, item.location
                );
            },
            [&](const nodes::FromUse& from_use) {
                project.add_module(
                    from_use.module_name, *module.source, item.location
                );
            },
            [&](const nodes::Func& func) {
                if (func.is_public) {
                    result.public_items.emplace(func.name, &func);
                }
            },
            [&](const nodes::Struct& struct_def) {
                result.public_items.emplace(struct_def.name, &struct_def);
            }
        );
    }
    return result;
}

namespace {
class Scopes {
public:
    Scopes(const Source& source, ErrorHandler& err_handler)
        : source_(&source), err_handler_(&err_handler) {
        push();
    }
    void push(bool is_loop = false, bool is_func = false) {
        auto& scope = scopes_.emplace_back();
        scope.is_loop = is_loop;
        scope.is_func = is_func;
    }
    void pop() { scopes_.pop_back(); }

    [[nodiscard]] ErrorHandler& err_handler() const { return *err_handler_; }
    [[nodiscard]] const Source& source() const { return *source_; }

    std::optional<Symbol> add(std::string_view name, Symbol symbol) {
        auto redefined = [&] -> std::optional<Symbol> {
            if (auto it = scopes_.back().symbols.find(name);
                it != scopes_.back().symbols.end()) {
                return it->second;
            }
            return std::nullopt;
        }();
        scopes_.back().symbols.emplace(name, symbol);
        return redefined;
    }

    void add(std::string_view name, Symbol symbol, Location location) {
        if (auto redefined = add(name, symbol)) {
            auto redefined_location = redefined->visit([](const auto* item) {
                return item->location;
            });
            err_handler_->report({
                .message = std::format("name '{}' already defined", name),
                .labels = {
                    {.source = source_,
                     .location = location,
                     .message = "this name already defined"},
                    {.source = source_,
                     .location = redefined_location,
                     .message = "defined here"}
                },
            });
        }
    }

    std::optional<std::uint8_t> from_str(
        std::string_view str, Location location
    ) const {
        std::uint8_t number = 0;
        auto result =
            std::from_chars(str.data(), str.data() + str.size(), number);
        if (result.ec != std::errc()) {
            err_handler_->report({
                .message = "error in number",
                .labels = {{.source = source_, .location = location}},
            });
            return std::nullopt;
        }
        return number;
    }

    [[nodiscard]] std::optional<Symbol> find(std::string_view name) const {
        for (const auto& scope : scopes_ | std::views::reverse) {
            if (auto it = scope.symbols.find(name); it != scope.symbols.end()) {
                return it->second;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<Symbol> find(
        std::string_view name, Location location
    ) const {
        return find(name).or_else([&] -> std::optional<Symbol> {
            if (name == "Nothing") return builtins::Nothing {};
            if (name == "None") return builtins::None {};
            if (name == "Bool") return builtins::Bool {};
            if (name.starts_with("Int")) {
                if (name.size() == 3) return builtins::Int {};
                auto bits_str = name.substr(3);
                return builtins::Int {from_str(bits_str, location)};
            }
            if (name.starts_with("UInt")) {
                if (name.size() == 4) return builtins::UInt {};
                auto bits_str = name.substr(4);
                return builtins::UInt {from_str(bits_str, location)};
            }
            if (name == "Float" || name == "Float64")
                return builtins::Float64 {};
            if (name == "Float32") return builtins::Float32 {};
            if (name == "Array") return builtins::Array {};
            if (name == "Ptr") return builtins::Ptr {};
            report_not_defined(name, location);
            return std::nullopt;
        });
    }
    void report_not_defined(std::string_view name, Location location) const {
        auto names = collect_names();
        auto best_name = find_best_name(names, name);
        auto notes = best_name
                         .and_then([&](auto best_name) {
                             return std::optional {std::vector {
                                 std::format("did you mean '{}'?", best_name)
                             }};
                         })
                         .value_or(std::vector<std::string> {});
        err_handler_->report({
            .message = std::format("name '{}' is not defined", name),
            .labels =
                {{.source = source_,
                  .location = location,
                  .message = "this name not found"}},
            .notes = std::move(notes),
        });
    }

    [[nodiscard]] bool in_loop() const {
        return std::ranges::any_of(
            scopes_ | std::views::reverse,
            [](const auto& scope) { return scope.is_loop; }
        );
    }

    [[nodiscard]] bool in_func() const {
        return std::ranges::any_of(
            scopes_ | std::views::reverse,
            [](const auto& scope) { return scope.is_func; }
        );
    }

private:
    [[nodiscard]] std::vector<std::string_view> collect_names() const {
        std::vector<std::string_view> names;
        for (const auto& scope : scopes_) {
            names.append_range(scope.symbols | std::views::keys);
        }
        return names;
    }

    static std::optional<std::string_view> find_best_name(
        std::span<const std::string_view> names, std::string_view name
    ) {
        return std::nullopt;
    }

    const Source* source_;
    ErrorHandler* err_handler_;

    struct Scope {
        std::unordered_map<std::string_view, Symbol> symbols;
        bool is_loop = false;
        bool is_func = false;
    };
    std::vector<Scope> scopes_;
};

Scopes collect(
    const nodes::Module& module, ModuleContext& context, Project& project
) {
    Scopes scopes(*module.source, project.err_handler());
    for (const auto& item : module.items) {
        item.visit(
            [&](const nodes::Use& use) {
                const auto& module_context =
                    project.module_context(use.module_name);
                auto* used_module = [&] {
                    auto it =
                        context.used_modules.find(use.module_name.front());
                    if (it == context.used_modules.end()) {
                        return &context.used_modules
                                    .emplace(
                                        use.module_name.front(),
                                        UsedModule {.location = item.location}
                                    )
                                    .first->second;
                    }
                    return &it->second;
                }();
                for (auto part : std::span(use.module_name).subspan(1)) {
                    if (auto it = used_module->submodules.find(part);
                        it != used_module->submodules.end()) {
                        used_module = &it->second;
                    } else {
                        used_module =
                            &used_module->submodules
                                 .emplace(
                                     part,
                                     UsedModule {.location = item.location}
                                 )
                                 .first->second;
                    }
                }
                used_module->module = &module_context;
            },
            [&](const nodes::FromUse& from_use) {
                auto used_module = project.module_context(from_use.module_name);
                for (const auto& use_item : from_use.items) {
                    auto it = used_module.public_items.find(use_item.name);
                    if (it == used_module.public_items.end()) {
                        project.err_handler().report({
                            .message = std::format(
                                "name '{}' is not found in module '{}'",
                                use_item.name,
                                from_use.module_name.join()
                            ),
                            .labels = {
                                {.source = module.source,
                                 .location = use_item.location,
                                 .message = "this used name not found"}
                            },
                        });
                    } else {
                        auto name = use_item.alias.value_or(use_item.name);
                        const auto& used_item = context.used_items.emplace_back(
                            std::make_unique<UsedItem>(
                                &used_module, it->second, use_item.location
                            )
                        );
                        scopes.add(name, used_item.get(), use_item.location);
                    }
                }
            },
            [&](const nodes::Func& func) {
                scopes.add(func.name, &func, func.location);
            },
            [&](const nodes::Struct& struct_def) {
                scopes.add(struct_def.name, &struct_def, struct_def.location);
            }
        );
    }
    return scopes;
}

void resolve_expr(
    const nodes::Expr& expr, Scopes& scopes, ModuleContext& context
) {
    expr.visit(
        [](const nodes::Expr::Literal&) {},
        [&](const nodes::Expr::Name& name) {
            if (auto symbol = scopes.find(name.name, expr.location)) {
                context.var_refs.emplace(&name, *symbol);
            }
        },
        [&](const nodes::Expr::Binary& binary) {
            resolve_expr(*binary.left, scopes, context);
            resolve_expr(*binary.right, scopes, context);
        },
        [&](const nodes::Expr::Unary& unary) {
            resolve_expr(*unary.operand, scopes, context);
        },
        [&](const nodes::Expr::Comparison& comparison) {
            for (const auto& operand : comparison.operands) {
                resolve_expr(*operand, scopes, context);
            }
        },
        [&](const nodes::Expr::Call& call) {
            resolve_expr(*call.value, scopes, context);
            for (const auto& arg : call.args) {
                resolve_expr(*arg.value, scopes, context);
            }
        },
        [&](const nodes::Expr::GetItem& get_item) {
            resolve_expr(*get_item.value, scopes, context);
            for (const auto& arg : get_item.args) {
                resolve_expr(*arg, scopes, context);
            }
        },
        [&](const nodes::Expr::GetAttr& get_attr) {
            resolve_expr(*get_attr.value, scopes, context);
        },
        [&](const nodes::Expr::Array& array) {
            for (const auto& item : array.items) {
                resolve_expr(*item, scopes, context);
            }
        },
        [&](const nodes::Expr::As& as) {
            resolve_expr(*as.value, scopes, context);
            resolve_expr(*as.type, scopes, context);
        },
        [&](const nodes::Expr::Spec& spec) {
            resolve_expr(*spec.type, scopes, context);
        }
    );
}

void resolve_lvalue(
    const nodes::Expr& expr,
    Scopes& scopes,
    ModuleContext& context,
    const nodes::Stmt* assign = nullptr
) {
    expr.visit(
        [&](const nodes::Expr::Name& name) {
            if (auto symbol = scopes.find(name.name)) {
                if (!symbol->is<const nodes::Stmt*>()) {
                    scopes.err_handler().report({
                        .message = std::format("name '{}' is not assignable", name.name),
                        .labels = {{
                            .source = &scopes.source(),
                            .location = expr.location,
                        }},
                    });
                } else {
                    context.var_refs.emplace(&name, *symbol);
                }
            } else if (assign) {
                scopes.add(name.name, assign);
            } else {
                scopes.report_not_defined(name.name, expr.location);
            }
        },
        [&](const nodes::Expr::GetItem& get_item) {
            resolve_lvalue(*get_item.value, scopes, context);
            for (const auto& item : get_item.args) {
                resolve_expr(*item, scopes, context);
            }
        },
        [&](const nodes::Expr::GetAttr& get_attr) {
            resolve_lvalue(*get_attr.value, scopes, context);
        },
        [&](const auto&) {
            scopes.err_handler().report({
                .message = "expression is not assignable",
                .labels = {{
                    .source = &scopes.source(),
                    .location = expr.location,
                }},
            });
        }
    );
}

void resolve_stmt(
    const nodes::Stmt& stmt, Scopes& scopes, ModuleContext& context
) {
    stmt.visit(
        [&](const nodes::Stmt::Expr& expr) {
            resolve_expr(*expr.expr, scopes, context);
        },
        [&](const nodes::Stmt::Var& var) {
            scopes.add(var.name, &stmt, stmt.location);
            if (var.type) {
                resolve_expr(*var.type, scopes, context);
            }
            if (var.init) {
                resolve_expr(*var.init, scopes, context);
            }
        },
        [&](const nodes::Stmt::Block& block) {
            scopes.push();
            for (const auto& item : block.stmts) {
                resolve_stmt(*item, scopes, context);
            }
            scopes.pop();
        },
        [&](const nodes::Stmt::If& if_stmt) {
            resolve_expr(*if_stmt.cond, scopes, context);
            scopes.push();
            resolve_stmt(*if_stmt.then_block, scopes, context);
            scopes.pop();
            if (if_stmt.else_block) {
                scopes.push();
                resolve_stmt(*if_stmt.else_block, scopes, context);
                scopes.pop();
            }
        },
        [&](const nodes::Stmt::While& while_stmt) {
            resolve_expr(*while_stmt.cond, scopes, context);
            scopes.push(true);
            resolve_stmt(*while_stmt.body, scopes, context);
            scopes.pop();
        },
        [&](const nodes::Stmt::Return&) {
            if (!scopes.in_func()) {
                scopes.err_handler().report({
                    .message = "return outside function",
                    .labels = {
                        {.source = &scopes.source(), .location = stmt.location}
                    },
                });
            }
        },
        [&](const nodes::Stmt::Break) {
            if (!scopes.in_loop()) {
                scopes.err_handler().report({
                    .message = "break outside loop",
                    .labels = {
                        {.source = &scopes.source(), .location = stmt.location}
                    },
                });
            }
        },
        [&](const nodes::Stmt::Continue) {
            if (!scopes.in_loop()) {
                scopes.err_handler().report({
                    .message = "continue outside loop",
                    .labels = {
                        {.source = &scopes.source(), .location = stmt.location}
                    },
                });
            }
        },
        [&](const nodes::Stmt::Assign& assign) {
            resolve_expr(*assign.value, scopes, context);
            for (const auto& target : assign.targets) {
                resolve_lvalue(*target, scopes, context, &stmt);
            }
        },
        [&](const nodes::Stmt::OpAssign& op) {
            resolve_expr(*op.value, scopes, context);
            resolve_lvalue(*op.target, scopes, context, &stmt);
        }
    );
}

}

void resolve_inner(
    const nodes::Module& module, ModuleContext& context, Project& project
) {
    auto scopes = collect(module, context, project);
    for (const auto& item : module.items) {
        if (auto func = item.get_func()) {
            if (func->body) {
                scopes.push(false, true);
                for (const auto& arg : func->args) {
                    scopes.add(arg.name, &arg, arg.location);
                }
                resolve_stmt(*func->body, scopes, context);
                scopes.pop();
            }
        } else if (auto struct_def = item.get_struct()) {
            scopes.push();
            for (const auto& field : struct_def->fields) {
                if (field.type) {
                    resolve_expr(*field.type, scopes, context);
                }
            }
            scopes.pop();
        }
    }
}
}