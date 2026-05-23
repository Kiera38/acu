#include "type_analyze.h"

#include "index.h"
#include "project.h"
#include "semanal/ir.h"
#include "types.h"

namespace acu::semanal {
PackageAnalyzer::PackageAnalyzer(
    const Packages& project, ir::Package& package, ErrorHandler& err_handler
)
    : project_(&project),
      package_(&package),
      err_handler_(&err_handler),
      func_map_(package.funcs().size(), NullRef),
      used_func_map_(package.used_funcs().size(), NullRef) {}

types::OptionalTypeId get_func_type(
    const ir::Func& func,
    types::OptionalSpecType return_type,
    ir::Package& package
) {
    if (!return_type.type) return NullRef;
    std::vector<types::Type::FuncParam> param_types;
    param_types.reserve(func.params.size);
    for (const auto& param : package.params(func.params)) {
        if (!param.type.type) return NullRef;
        param_types.push_back(
            {.name = param.name, .type = param.type.as_type()}
        );
    }
    return package.types().add_func({
        .params = std::move(param_types),
        .min_pos_args = func.min_pos_args,
        .max_pos_args = func.max_pos_args,
        .return_type = return_type.as_type(),
    });
}

ir::AnalyzedPackage PackageAnalyzer::analyze() {
    std::unordered_map<
        ir::FuncRef,
        ir::AFuncRef,
        hash<ir::FuncRef>,
        equal_to<ir::FuncRef>>
        public_funcs;
    for (auto ref : package_->funcs().indices()) {
        const auto& func = package_->func(ref);
        if (func.is_extern) {
            auto aref = funcs_.emplace_back(
                ir::AFunc {
                    .func = ref,
                    .type =
                        get_func_type(func, func.return_type, *package_).ref(),
                    .types = {RefRange<types::SpecType, ir::InstRef> {}},
                    .comparator_types = {
                        RefRange<types::TypeId, ir::ComparatorRef> {}
                    },
                }
            );
            func_map_[ref] = aref;
            if (func.is_public) {
                public_funcs[ref] = aref;
            }
            continue;
        }
        if (func.is_public) {
            auto aref = funcs_.emplace_back(
                std::make_unique<TypeAnalyzer>(*this, ref, func)
            );
            analyze_funcs_.push_back(aref);
            func_map_[ref] = aref;
            public_funcs[ref] = aref;
        }
    }
    while (!analyze_funcs_.empty()) {
        auto aref = analyze_funcs_.front();
        analyze_funcs_.pop_front();
        auto& analyzer = funcs_[aref].get<std::unique_ptr<TypeAnalyzer>>();
        if (!analyzer->propagate()) {
            funcs_[aref] = analyzer->get_types();
        } else {
            analyze_funcs_.push_back(aref);
        }
    }
    IndexVector<ir::AFunc, ir::AFuncRef> funcs;
    funcs.reserve(funcs_.size());
    for (const auto& func : funcs_) {
        funcs.push_back(func.get<ir::AFunc>());
    }
    return {
        .ir_package = package_,
        .funcs = std::move(funcs),
        .used_funcs = std::move(used_funcs_),
        .public_funcs = std::move(public_funcs)
    };
}

ir::AFuncRef PackageAnalyzer::func(ir::FuncRef ref) {
    if (func_map_[ref]) {
        return *func_map_[ref];
    }
    const auto& func = package_->func(ref);
    auto aref =
        funcs_.emplace_back(std::make_unique<TypeAnalyzer>(*this, ref, func));
    analyze_funcs_.push_back(aref);
    func_map_[ref] = aref;
    return aref;
}

types::OptionalSpecType PackageAnalyzer::func_return_type(ir::AFuncRef ref) {
    return funcs_[ref].visit(
        [&](const std::unique_ptr<TypeAnalyzer>& analyzer) {
            return analyzer->func_->return_type;
        },
        [&](const ir::AFunc& func) {
            return package_->types()
                .get(func.type)
                .data.get<types::Type::Func>()
                .return_type.as_optional();
        }
    );
}

types::OptionalTypeId PackageAnalyzer::func_type(ir::AFuncRef ref) {
    return funcs_[ref].visit(
        [&](const std::unique_ptr<TypeAnalyzer>& analyzer) {
            return get_func_type(
                *analyzer->func_, analyzer->return_type_, *package_
            );
        },
        [&](const ir::AFunc& func) -> types::OptionalTypeId {
            return func.type;
        }
    );
}

ir::AUsedFuncRef PackageAnalyzer::used_func(ir::UsedFuncRef ref) {
    if (used_func_map_[ref]) {
        return *used_func_map_[ref];
    }
    const auto& func = package_->used_func(ref);
    const auto& package_types = project_->package(func.package).types();
    const auto& apackage = project_->apackage(func.package);
    const auto& aref = apackage.public_funcs.at(func.func);
    auto type =
        package_->types().copy(package_types, apackage.funcs[aref].type);
    auto aused_ref = used_funcs_.push_back({
        .package = func.package,
        .func = aref,
        .type = type,
    });
    used_func_map_[ref] = aused_ref;
    return aused_ref;
}
types::TypeId PackageAnalyzer::used_func_type(ir::AUsedFuncRef ref) {
    return used_funcs_[ref].type;
}

ir::AnalyzedPackage type_analyze(
    const Packages& project, ir::Package& package, ErrorHandler& err_handler
) {
    PackageAnalyzer analyzer(project, package, err_handler);
    return analyzer.analyze();
}
}