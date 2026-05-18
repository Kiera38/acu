#pragma once

#include "ir.h"
#include "type_utils.h"

namespace acu::semanal {

class TypeAnalyzer {
public:
    TypeAnalyzer(
        ir::Package& package, ir::FuncRef func_ref, ErrorHandler& err_handler
    );

    bool propagate();
    [[nodiscard]] ir::AFunc get_types();

private:
    void error(Location location, std::string message) const;
    [[nodiscard]] const types::Type::Func& get_func_type() const;
    void require_var(ir::InstRef ref, Location loc, std::string_view context);
    void propagate_range(ir::Block range);
    void propagate_inst(ir::InstRef ref);

    void handle_const(
        ir::InstRef ref, const ir::Inst::Const& data, Location loc
    );
    void handle_var_decl(
        ir::InstRef ref, const ir::Inst::VarDecl& data, Location loc
    );
    void handle_load_var(
        ir::InstRef ref, const ir::Inst::LoadVar& data, Location loc
    );
    void handle_load_param(
        ir::InstRef ref, const ir::Inst::LoadParam& data, Location loc
    );
    void handle_store(
        ir::InstRef ref, const ir::Inst::Store& data, Location loc
    );
    void handle_binary(
        ir::InstRef ref, const ir::Inst::Binary& data, Location loc
    );
    void handle_logical(
        ir::InstRef ref, const ir::Inst::Logical& data, Location loc
    );
    void handle_unary(
        ir::InstRef ref, const ir::Inst::Unary& data, Location loc
    );
    void handle_comparison(
        ir::InstRef ref, const ir::Inst::Comparison& data, Location loc
    );
    void handle_call(
        ir::InstRef ref, const ir::Inst& inst, const ir::Inst::Call& data
    );
    void handle_loop(ir::InstRef ref, const ir::Inst::Loop& data, Location loc);
    void handle_if(ir::InstRef ref, const ir::Inst::If& data, Location loc);
    void handle_return(
        ir::InstRef ref, const ir::Inst::Return& data, Location loc
    );
    void handle_address_of(
        ir::InstRef ref, const ir::Inst::AddressOf& data, Location loc
    );
    void handle_deref(
        ir::InstRef ref, const ir::Inst::Deref& data, Location loc
    );
    void handle_get_item(
        ir::InstRef ref, const ir::Inst::GetItem& data, Location loc
    );
    void handle_set_item(
        ir::InstRef ref, const ir::Inst::SetItem& data, Location loc
    );
    void handle_get_attr(
        ir::InstRef ref, const ir::Inst::GetAttr& data, Location loc
    );
    void handle_set_attr(
        ir::InstRef ref, const ir::Inst::SetAttr& data, Location loc
    );
    void handle_array(
        ir::InstRef ref, const ir::Inst::Array& data, Location loc
    );
    void handle_as(ir::InstRef ref, const ir::Inst::As& data, Location loc);

    void check_func(
        ir::InstRef ref, const ir::Inst& inst, const types::Type::Func& ft
    );
    void check_struct(
        ir::InstRef ref, const ir::Inst& inst, const types::Type::Struct& type
    );

    void add_type(ir::InstRef inst, types::SpecType type, Location loc);
    void copy_type(ir::InstRef src, ir::InstRef dest, Location loc);
    void lock_type(ir::InstRef inst, types::TypeId type, Location loc);
    void lock_type(ir::InstRef inst, types::SpecType type, Location loc);

    ir::Package* package_;
    IndexMap<ir::InstRef, TypeVar> type_vars_;
    IndexMap<ir::ComparatorRef, types::TypeId> comparator_types_;
    types::TypePool* type_pool_;
    types::TypeId func_type_id_ {};
    ir::Func* func_;
    ir::FuncRef func_ref_;
    ErrorHandler* err_handler_;
    bool changed_ = false;
    std::uint32_t current_inst_ = 0;
};
}