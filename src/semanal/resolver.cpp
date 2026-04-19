#include "resolver.h"

#include "package_name.h"


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
        const auto& mod = project_context_->module(ref);

        auto& module = [&] -> ir::Module& {
            if (ir_package_.name().size() == mod.source->name.size()) {
                root_context_ = Context(*mod.source);
                return ir_package_.root_module();
            } else {
                auto module_name = mod.source->name.back();
                module_contexts_.insert({module_name, Context(*mod.source)});
                return ir_package_.add_module(module_name);
            }
        }();
        set_context(mod.source->name);

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
        set_context(mod.source->name);

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

utils::Variant<Context*, UsedModule> Resolver::get_module_context(
    PackageNameRef module_name, Location location
) {
    if (module_name.size() < ir_package_.name().size()) {
        auto used = project_context_->module_package(module_name);
        return UsedModule {.package = used.first, .module = used.second};
    }
    for (const auto& [package_name, using_name] :
         std::views::zip(ir_package_.name(), module_name)) {
        if (package_name != using_name) {
            auto used = project_context_->module_package(module_name);
            return UsedModule {.package = used.first, .module = used.second};
        }
    }
    auto name = std::span(module_name).subspan(ir_package_.name().size());
    if (name.empty()) {
        if (root_context_.has_value()) {
            return &*root_context_;
        }
    } else if (name.size() == 1) {
        if (auto it = module_contexts_.find(name[0]);
            it != module_contexts_.end()) {
            return &it->second;
        }
    }
    auto used = project_context_->module_package(module_name);
    return UsedModule {.package = used.first, .module = used.second};
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
                        get_used_func(module.package, ref)
                    };
                },
                [&](types::TypeId type) -> std::optional<Context::ScopeEntry> {
                    return Context::ScopeEntry {
                        ir_package_.types().add_used_struct(
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
    size_t best_match = 0;

    for (const auto& imported : imported_modules_) {
        if (imported.path.size() >= path.size()) {
            continue;
        }
        if (!std::equal(
                imported.path.begin(), imported.path.end(), path.begin()
            )) {
            continue;
        }

        auto remaining = std::span(path).subspan(imported.path.size());
        if (remaining.empty()) {
            continue;
        }
        if (imported.path.size() < best_match) {
            continue;
        }

        if (auto entry = find_in_module_item(imported.module, remaining[0])) {
            if (remaining.size() == 1) {
                result = *entry;
                best_match = imported.path.size();
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
                        context_->add(item.alias.value_or(item.name), *entry);
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
                    [&](std::monostate) {},
                    [&](ir::FuncRef ref) {
                        context_->add(
                            item.alias.value_or(item.name),
                            {get_used_func(module.package, ref)}
                        );
                    },
                    [&](types::TypeId type) {
                        context_->add(
                            item.alias.value_or(item.name),
                            {ir_package_.types().add_used_struct(
                                types::Type::UsedStruct {
                                    .pool = &project_context_
                                                 ->package(module.package)
                                                 .types(),
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

types::TypeId Resolver::create_struct_def(
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

void Resolver::resolve_struct_def(const nodes::Struct& struct_node) {
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

ir::FuncRef Resolver::create_func_def(
    const nodes::Func& func_node, Location location
) {
    ir::Func ir_func(
        func_node.name, context_->source(), location, func_node.is_extern
    );
    auto func_ref = ir_package_.add(std::move(ir_func));
    context_->add(func_node.name, {func_ref});
    return func_ref;
}

void Resolver::resolve_func_def(const nodes::Func& func_node) {
    auto* entry = context_->find(func_node.name);
    if (entry) {
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
                        "in extern function return type must have val "
                        "specifier"
                    );
                }
                return_type.specifier = types::Specifier::Val;
            } else if (return_type.specifier == types::Specifier::None) {
                return_type.specifier = types::Specifier::Val;
            }
        }
        ir_func.set_type(
            ir_params,
            func_node.min_pos_args,
            func_node.max_pos_args,
            return_type
        );

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

ir::UsedFuncRef Resolver::get_used_func(
    ir::PackageRef package_ref, ir::FuncRef ref
) {
    ir::UsedFunc used_func {.package = package_ref, .func = ref};
    if (auto it = used_funcs_.find(used_func); it != used_funcs_.end()) {
        return it->second;
    }
    auto& package = project_context_->package(package_ref);
    used_func.type =
        ir_package_.types().copy(package.types(), package.func_type(ref));
    auto used_func_ref = ir_package_.add(used_func);
    used_funcs_.insert({used_func, used_func_ref});
    return used_func_ref;
}

}