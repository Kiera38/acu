#include "specifiers.h"

#include <ranges>
#include <vector>

#include "index.h"
#include "ir.h"
#include "semanal/types.h"

namespace acu::refanal {
namespace {
using types::Specifier;

void set_defaults(ir::Module& module, ir::Func& func) {
    auto insts = func.insts();

    // 1. Initial pass: set defaults
    for (auto i : insts.indices()) {
        auto& inst = insts[i];
        if (!inst.has_value() && !inst.data.is<ir::Inst::VarDecl>()) continue;

        inst.type.specifier = inst.data.visit(
            [&](ir::Inst::Const&) { return Specifier::Val; },
            [&](ir::Inst::Binary&) { return Specifier::Val; },
            [&](ir::Inst::Unary&) { return Specifier::Val; },
            [&](ir::Inst::Comparison&) { return Specifier::Val; },
            [&](ir::Inst::CreateStruct&) { return Specifier::Val; },
            [&](ir::Inst::Array&) { return Specifier::Val; },
            [&](ir::Inst::AddressOf&) { return Specifier::Val; },
            [&](ir::Inst::LoadParam& lp) { return Specifier::Let; },
            [&](ir::Inst::Call& c) {
                auto func_type = module.types().get(insts[c.value].type.type);
                auto spec = func_type.data.get<types::Type::Func>()
                                .return_type.specifier;
                if (spec != Specifier::None) return spec;
                return Specifier::Val;
            },
            [&](ir::Inst::Cast& c) { return inst.type.specifier; },
            [&](ir::Inst::VarDecl&) { return Specifier::Let; },
            [&](ir::Inst::LoadVar&) { return Specifier::Let; },
            [&](ir::Inst::GetField&) { return Specifier::Let; },
            [&](ir::Inst::GetItem&) { return Specifier::Let; },
            [&](ir::Inst::Deref&) { return Specifier::Let; },
            [&](auto&) { return Specifier::None; }
        );
    }
}

void infer_specifiers(
    ir::Module& module, ir::Func& func, ErrorHandler& err_handler
) {
    auto insts = func.insts();
    set_defaults(module, func);

    // Capability analysis: which instructions CAN be Var?
    IndexVector<bool, ir::InstRef> can_be_var(insts.size(), false);
    for (auto i : insts.indices()) {
        auto& inst = insts[i];
        can_be_var[i] = inst.data.visit(
            [&](ir::Inst::VarDecl&) -> bool { return true; },
            [&](ir::Inst::LoadParam& lp) -> bool {
                return func.param(lp.param).type.specifier == Specifier::Var;
            },
            [&](auto&) -> bool { return false; }
        );
    }

    bool cap_changed = true;
    while (cap_changed) {
        cap_changed = false;
        for (auto i : insts.indices()) {
            if (can_be_var[i]) continue;
            auto& inst = insts[i];
            bool possible = inst.data.visit(
                [&](ir::Inst::LoadVar& lv) -> bool {
                    return can_be_var[lv.var];
                },
                [&](ir::Inst::GetField& gf) -> bool {
                    auto& type = module.types().get(insts[gf.value].type.type);
                    auto& struct_type = type.data.get<types::Type::Struct>();
                    return struct_type.fields[gf.index].type.specifier !=
                           Specifier::Let;
                },
                [&](ir::Inst::GetItem& gi) -> bool {
                    auto& type =  module.types().get(insts[gi.value].type.type);
                    if(const auto& arr_type = type.data.get_if<types::Type::Array>()) {
                        return arr_type->item.specifier != Specifier::Let;
                    } else if(const auto& ptr_type = type.data.get_if<types::Type::Ptr>()) {
                        return ptr_type->type.specifier != Specifier::Let;
                    }
                    return false;
                },
                [&](ir::Inst::Deref& d) -> bool {
                    auto& type = module.types().get(insts[d.value].type.type);
                    auto& ptr_type = type.data.get<types::Type::Ptr>();
                    return ptr_type.type.specifier != Specifier::Let;
                },
                [&](ir::Inst::Cast& c) -> bool {
                    return inst.type.specifier != Specifier::Let;
                },
                [&](auto&) -> bool { return false; }
            );
            if (possible) {
                can_be_var[i] = true;
                cap_changed = true;
            }
        }
    }

    // 2. Propagate Var
    bool changed = true;
    auto make_var =
        [&](ir::InstRef ref, Location loc, std::string_view context) {
            auto& inst = insts[ref];
            if (inst.type.specifier == Specifier::Let) {
                if (can_be_var[ref]) {
                    inst.type.specifier = Specifier::Var;
                    changed = true;
                    return true;
                } else {
                    err_handler.error(
                        func.source(),
                        loc,
                        "cannot mutate immutable value",
                        std::string("this ") + std::string(context) +
                            " requires a mutable reference (var), but the "
                            "value is immutable (let)"
                    );
                    return false;
                }
            }
            return inst.type.specifier == Specifier::Var;
        };

    while (changed) {
        changed = false;
        for (auto i : insts.indices()) {
            auto& inst = insts[i];
            inst.data.visit(
                [&](ir::Inst::Store& s) {
                    if(insts[s.var].type.specifier == Specifier::Var) {
                        make_var(s.value, inst.location, "assignment");
                    }
                },
                [&](ir::Inst::SetField& sf) {
                    auto& var = insts[sf.var];
                    auto& struct_type = module.types()
                                            .get(var.type.type)
                                            .data.get<types::Type::Struct>();
                    auto field_spec =
                        struct_type.fields[sf.index].type.specifier;
                    if (field_spec == Specifier::Var) {
                        make_var(
                            sf.value, inst.location, "field assignment value"
                        );
                    } else if (field_spec == Specifier::Val) {
                        make_var(sf.var, inst.location, "val field assignment");
                    }
                },
                [&](ir::Inst::SetItem& si) {
                    auto& var = insts[si.var];
                    auto& type = module.types().get(var.type.type);
                    Specifier item_spec = [&] {
                        if (auto ptr = type.data.get_if<types::Type::Ptr>()) {
                            return ptr->type.specifier;
                        } else {
                            auto array_item_spec =
                                type.data.get<types::Type::Array>()
                                    .item.specifier;
                            make_var(
                                si.var, inst.location, "val item assignment"
                            );
                            return array_item_spec;
                        }
                    }();
                    if (item_spec == Specifier::Var) {
                        make_var(
                            si.value, inst.location, "item assignment value"
                        );
                    }
                },
                [&](ir::Inst::Call& call) {
                    auto& func_type = module.types()
                                          .get(insts[call.value].type.type)
                                          .data.get<types::Type::Func>();
                    auto args = func.inst_refs(call.args);
                    for (const auto& [param_type, arg_ref] :
                         std::views::zip(func_type.params, args)) {
                        if (param_type.specifier == Specifier::Var) {
                            make_var(
                                arg_ref, inst.location, "function call argument"
                            );
                        }
                    }
                },
                [&](ir::Inst::CreateStruct& cs) {
                    auto& struct_type = module.types()
                                            .get(cs.struct_type)
                                            .data.get<types::Type::Struct>();
                    auto args = func.inst_refs(cs.args);
                    for (const auto& [field, arg_ref] :
                         std::views::zip(struct_type.fields, args)) {
                        if (field.type.specifier == Specifier::Var) {
                            make_var(
                                arg_ref,
                                inst.location,
                                "struct initialization field"
                            );
                        }
                    }
                },
                [&](ir::Inst::Array& arr) {
                    auto item_spec = module.types()
                                         .get(inst.type.type)
                                         .data.get<types::Type::Array>()
                                         .item.specifier;
                    if (item_spec == Specifier::Var) {
                        for (auto item_ref : func.inst_refs(arr.items)) {
                            make_var(item_ref, inst.location, "array element");
                        }
                    }
                },
                [&](auto&) {}
            );
        }
    }
}
}

void infer_specifiers(ir::Module& module, ErrorHandler& err_handler) {
    for (auto& func : module.funcs()) {
        infer_specifiers(module, func, err_handler);
    }
}

}
