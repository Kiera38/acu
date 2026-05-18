#include "type_analyze.h"

#include "index.h"
#include "semanal/ir.h"
#include "types.h"

namespace acu::semanal {
PackageAnalyzer::PackageAnalyzer(
    ir::Package& package, ErrorHandler& err_handler
)
    : package_(&package),
      err_handler_(&err_handler),
      func_map_(package.funcs().size(), NullRef) {}

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
                    .type = package_->func_type(ref),
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
                std::make_unique<TypeAnalyzer>(
                    *this, ref, func, package_->func_type(ref)
                )
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
        .funcs_ = std::move(funcs),
        .public_funcs = std::move(public_funcs)
    };
}

ir::AFuncRef PackageAnalyzer::func(ir::FuncRef ref) {
    if (func_map_[ref]) {
        return *func_map_[ref];
    }
    const auto& func = package_->func(ref);
    auto aref = funcs_.emplace_back(
        std::make_unique<TypeAnalyzer>(
            *this, ref, func, package_->func_type(ref)
        )
    );
    analyze_funcs_.push_back(aref);
    func_map_[ref] = aref;
    return aref;
}

types::SpecType PackageAnalyzer::func_return_type(ir::AFuncRef ref) {
    return funcs_[ref].visit(
        [&](const std::unique_ptr<TypeAnalyzer>& analyzer) {
            return analyzer->get_func_type().return_type;
        },
        [&](const ir::AFunc& func) {
            return package_->types()
                .get(func.type)
                .data.get<types::Type::Func>()
                .return_type;
        }
    );
}

types::TypeId PackageAnalyzer::func_type(ir::AFuncRef ref) {
    return funcs_[ref].visit(
        [&](const std::unique_ptr<TypeAnalyzer>& analyzer) {
            return analyzer->func_type_id_;
        },
        [&](const ir::AFunc& func) { return func.type; }
    );
}

types::TypeId PackageAnalyzer::used_func_type(ir::UsedFuncRef ref) {
    return package_->used_func(ref).type;
}

ir::AnalyzedPackage type_analyze(
    ir::Package& package, ErrorHandler& err_handler
) {
    PackageAnalyzer analyzer(package, err_handler);
    return analyzer.analyze();
}
}