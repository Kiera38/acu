#include "resolver.h"

#include "package_name.h"
#include "semanal/ir.h"

namespace acu::semanal {
namespace {
std::size_t levenshtein_distance(std::string_view s1, std::string_view s2) {
    if (s1.size() < s2.size()) return levenshtein_distance(s2, s1);
    if (s2.empty()) return s1.size();

    std::vector<std::size_t> v(s2.size() + 1);
    for (std::size_t i = 0; i <= s2.size(); ++i) v[i] = i;

    for (std::size_t i = 0; i < s1.size(); ++i) {
        std::size_t prev_v_j = v[0];
        v[0] = i + 1;
        for (std::size_t j = 0; j < s2.size(); ++j) {
            std::size_t next_v_j = v[j + 1];
            std::size_t cost = (s1[i] == s2[j]) ? 0 : 1;
            v[j + 1] = std::min({v[j] + 1, v[j + 1] + 1, prev_v_j + cost});
            prev_v_j = next_v_j;
        }
    }
    return v[s2.size()];
}
}

const Context::ScopeEntry* Context::find(std::string_view name) const {
    for (const auto& it : std::ranges::reverse_view(scopes_stack_)) {
        auto entry_it = it.find(name);
        if (entry_it != it.end()) {
            return &entry_it->second;
        }
    }
    return nullptr;
}

std::vector<std::string_view> Context::get_all_names() const {
    std::vector<std::string_view> names;
    for (const auto& scope : scopes_stack_) {
        for (const auto& [name, _] : scope) {
            names.push_back(name);
        }
    }
    return names;
}

std::string Context::suggest_similar_name(std::string_view name) {
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

Resolver::Resolver(
    PackageName package_name,
    std::span<const ModuleRef> modules,
    const Packages& project_context,
    ErrorHandler& err_handler
)
    : ir_package_(std::move(package_name)),
      modules_(modules),
      project_context_(&project_context),
      err_handler_(&err_handler) {}

ir::Package Resolver::resolve() {
    for (auto ref : modules_) {
        auto& mod = project_context_->module(ref);
        const bool is_root =
            mod.source->name().size() == ir_package_.name().size();

        ir::Module& module =
            is_root ? [&] {
                root_context_ = Context(*mod.source);
                return std::ref(ir_package_.root_module());
            }()
                    : [&] {
                          auto module_name = mod.source->name().back();
                          module_contexts_.emplace(
                              module_name, Context(*mod.source)
                          );
                          return std::ref(ir_package_.add_module(module_name));
                      }();

        set_context(mod.source->name());

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

    for (auto ref : modules_) {
        const auto& mod = project_context_->module(ref);
        set_context(mod.source->name());

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

void Resolver::set_context(PackageNameRef module_name) {
    if (module_name.size() == ir_package_.name().size()) {
        context_ = &*root_context_;
    } else {
        context_ = &module_contexts_.at(module_name.back());
    }
}

void Resolver::report_redefinition(
    std::string_view name,
    Location location,
    const Context::ScopeEntry& existing
) {
    err_handler_->error(
        context_->source(),
        location,
        std::format("redefinition of '{}'", name),
        "",
        {{.source = &context_->source(),
          .location = existing.location,
          .message = "previous definition is here"}}
    );
}

utils::Variant<Context*, UsedModule> Resolver::get_module_context(
    PackageNameRef module_name, Location location
) {
    const auto& current_package = ir_package_.name();

    bool is_internal =
        module_name.size() >= current_package.size() &&
        std::equal(
            current_package.begin(), current_package.end(), module_name.begin()
        );

    if (is_internal) {
        auto relative_path =
            std::span(module_name).subspan(current_package.size());

        if (relative_path.empty()) {
            if (root_context_) return &*root_context_;
        } else if (relative_path.size() == 1) {
            if (auto it = module_contexts_.find(relative_path[0]);
                it != module_contexts_.end()) {
                return &it->second;
            }
        }
    }

    auto [pkg_ref, mod_ptr] = project_context_->module_package(module_name);
    return UsedModule {.package = pkg_ref, .module = mod_ptr};
}

bool Resolver::flatten_module_path(const nodes::Expr& expr, PackageName& path) {
    if (auto* name_node = expr.value.get_if<nodes::Expr::Name>()) {
        path.push_back(name_node->name);
        return true;
    }
    if (auto* node = expr.value.get_if<nodes::Expr::GetAttr>()) {
        if (!flatten_module_path(*node->value, path)) {
            return false;
        }
        path.push_back(node->name);
        return true;
    }
    return false;
}

std::optional<Context::ScopeEntry> Resolver::find_in_module_item(
    const utils::Variant<Context*, UsedModule>& module,
    std::string_view item_name
) {
    return module.visit(
        [&](Context* module_context) -> std::optional<Context::ScopeEntry> {
            if (auto entry = module_context->find(item_name)) {
                return *entry;
            }
            return std::nullopt;
        },
        [&](UsedModule module) -> std::optional<Context::ScopeEntry> {
            return module.module->find(item_name).visit(
                [&](std::monostate) -> std::optional<Context::ScopeEntry> {
                    return std::nullopt;
                },
                [&](ir::FuncRef ref) -> std::optional<Context::ScopeEntry> {
                    return Context::ScopeEntry {
                        .data = get_used_func(module.package, ref)
                    };
                },
                [&](types::TypeId type) -> std::optional<Context::ScopeEntry> {
                    return Context::ScopeEntry {
                        .data = ir_package_.types().add_used_struct(
                            types::Type::UsedStruct {
                                .pool =
                                    &project_context_->package(module.package)
                                         .types(),
                                .type = type
                            }
                        )
                    };
                }
            );
        }
    );
}

std::optional<Context::ScopeEntry> Resolver::find_in_imported_module_chain(
    PackageNameRef path
) {
    std::optional<Context::ScopeEntry> result;
    size_t best_match_size = 0;

    for (const auto& imported : imported_modules_) {
        if (imported.path.size() >= path.size() ||
            imported.path.size() < best_match_size) {
            continue;
        }

        if (std::equal(
                imported.path.begin(), imported.path.end(), path.begin()
            )) {
            auto remaining = std::span(path).subspan(imported.path.size());
            if (auto entry =
                    find_in_module_item(imported.module, remaining[0])) {
                if (remaining.size() == 1) {
                    result = *entry;
                    best_match_size = imported.path.size();
                }
            }
        }
    }

    return result;
}

void Resolver::resolve_using(const nodes::Use& use, Location location) {
    auto module = get_module_context(use.module_name, location);
    module.visit(
        [&](Context* module_context) {
            if (module_context) {
                imported_modules_.push_back(
                    ImportedModule {
                        .path = use.module_name, .module = module_context
                    }
                );
            }
        },
        [&](UsedModule module) {
            imported_modules_.push_back(
                ImportedModule {.path = use.module_name, .module = module}
            );
        }
    );
}

void Resolver::resolve_using(const nodes::FromUse& use, Location location) {
    auto module = get_module_context(use.module_name, location);
    module.visit(
        [&](Context* module_context) {
            if (module_context) {
                for (const auto& item : use.items) {
                    if (auto entry = module_context->find(item.name)) {
                        auto alias = item.alias.value_or(item.name);
                        if (auto existing = context_->add(
                                alias,
                                {.data = entry->data, .location = item.location}
                            )) {
                            report_redefinition(
                                alias, item.location, *existing
                            );
                        }
                    } else {
                        err_handler_->error(
                            context_->source(),
                            item.location,
                            std::format(
                                "name '{}' not found in module '{}'",
                                item.name,
                                use.module_name.join()
                            )
                        );
                    }
                }
            }
        },
        [&](UsedModule module) {
            for (const auto& item : use.items) {
                module.module->find(item.name).visit(
                    [&](std::monostate) {
                        err_handler_->error(
                            context_->source(),
                            item.location,
                            std::format(
                                "name '{}' not found in module '{}'",
                                item.name,
                                use.module_name.join()
                            )
                        );
                    },
                    [&](ir::FuncRef ref) {
                        auto alias = item.alias.value_or(item.name);
                        if (auto existing = context_->add(
                                alias,
                                {.data = get_used_func(module.package, ref),
                                 .location = item.location}
                            )) {
                            report_redefinition(
                                alias, item.location, *existing
                            );
                        }
                    },
                    [&](types::TypeId type) {
                        auto alias = item.alias.value_or(item.name);
                        if (auto existing = context_->add(
                                alias,
                                {.data = ir_package_.types().add_used_struct(
                                     types::Type::UsedStruct {
                                         .pool = &project_context_
                                                      ->package(module.package)
                                                      .types(),
                                         .type = type
                                     }
                                 ),
                                 .location = item.location}
                            )) {
                            report_redefinition(
                                alias, item.location, *existing
                            );
                        }
                    }
                );
            }
        }
    );
}

types::TypeId Resolver::create_struct_def(
    const nodes::Struct& struct_node, Location location
) {
    auto type_id = ir_package_.types().add_struct({
        .name = struct_node.name,
        .source = &context_->source(),
        .location = location,
    });
    if (auto existing = context_->add(struct_node.name, {type_id, location})) {
        report_redefinition(struct_node.name, location, *existing);
    }
    return type_id;
}

void Resolver::resolve_struct_def(const nodes::Struct& struct_node) {
    auto entry = context_->find(struct_node.name);
    if (!entry || !entry->data.is<types::TypeId>()) return;

    auto type_id = entry->data.get<types::TypeId>();
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

void Resolver::resolve_func_def(const nodes::Func& func_node) {
    auto* entry = context_->find(func_node.name);
    if (!entry || !entry->data.is<ir::FuncRef>()) return;

    ir::Func& ir_func = ir_package_.func(entry->data.get<ir::FuncRef>());
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
                    "in extern function return type must have val specifier"
                );
            }
            return_type.specifier = types::Specifier::Val;
        } else if (return_type.specifier == types::Specifier::None) {
            return_type.specifier = types::Specifier::Val;
        }
    }

    ir_func.params = ir_package_.add_params(ir_params);
    ir_func.min_pos_args = func_node.min_pos_args;
    ir_func.max_pos_args = func_node.max_pos_args;
    ir_func.return_type = return_type;

    if (!func_node.is_extern && func_node.body) {
        context_->push();
        for (size_t i = 0; i < ir_params.size(); ++i) {
            context_->add(
                func_node.args[i].name,
                {.data = {ir::ParamRef {static_cast<std::uint32_t>(i)}},
                 .location = func_node.args[i].location}
            );
        }
        ir_func.insts.start = ir_package_.last_inst().index + 1;
        ir_func.comparators.start = ir_package_.next_comparator().index;
        resolve_stmt(*func_node.body, ir_func);
        ir_func.insts.size =
            ir_package_.last_inst().index + 1 - ir_func.insts.start;
        ir_func.comparators.size =
            ir_package_.next_comparator().index - ir_func.comparators.start;
        context_->pop();
    }
}

ir::FuncRef Resolver::create_func_def(
    const nodes::Func& func_node, Location location
) {
    ir::Func ir_func {
        .source = &context_->source(),
        .location = location,
        .name = func_node.name,
        .is_extern = func_node.is_extern,
        .is_public = func_node.is_public
    };
    auto func_ref = ir_package_.add(ir_func);
    if (auto existing = context_->add(func_node.name, {func_ref, location})) {
        report_redefinition(func_node.name, location, *existing);
    }
    return func_ref;
}

ir::UsedFuncRef Resolver::get_used_func(
    ir::PackageRef package_ref, ir::FuncRef ref
) {
    ir::UsedFunc used_func {.package = package_ref, .func = ref};
    if (auto it = used_funcs_.find(used_func); it != used_funcs_.end()) {
        return it->second;
    }
    auto& package = project_context_->package(package_ref);
    auto used_func_ref = ir_package_.add(used_func);
    used_funcs_.insert({used_func, used_func_ref});
    return used_func_ref;
}

}