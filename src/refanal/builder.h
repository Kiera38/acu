#pragma once

#include "ir.h"

namespace acu::refanal::ir {

class PlaceBuilder {
public:
    PlaceBuilder(Func& func, LocalRef local) : func_(&func), local_(local) {}
    PlaceBuilder(Func& func, Place p) : func_(&func), local_(p.local) {
        auto existing = func.projections(p.projections);
        projections_.assign(existing.begin(), existing.end());
    }

    PlaceBuilder& field(std::uint32_t index) {
        projections_.push_back(
            {.kind = Projection::Kind::Field, .index = index}
        );
        return *this;
    }

    PlaceBuilder& index(LocalRef idx_local) {
        projections_.push_back(
            {.kind = Projection::Kind::Index, .index = idx_local.index}
        );
        return *this;
    }

    PlaceBuilder& deref() {
        projections_.push_back({.kind = Projection::Kind::Deref, .index = 0});
        return *this;
    }

    [[nodiscard]] Place build() const {
        return Place {
            .local = local_, .projections = func_->add_projections(projections_)
        };
    }

private:
    Func* func_;
    LocalRef local_;
    std::vector<Projection> projections_;
};

class Builder {
public:
    explicit Builder(Func& func) : func_(&func) {}

    Func& func() { return *func_; }

    // --- Blocks ---
    BlockRef add_block() { return func_->add_block(Block {}); }
    void set_block(BlockRef block) { current_block_ = block; }
    [[nodiscard]] BlockRef current_block() const { return current_block_; }

    // --- Locals ---
    LocalRef add_local(types::SpecType type, std::string_view name = "") {
        return func_->add_local({.name = name, .type = type});
    }

    // --- Places & Projections ---
    [[nodiscard]] Place place(LocalRef local) const {
        return Place {local, {}};
    }

    PlaceBuilder build_place(LocalRef local) {
        return PlaceBuilder {*func_, local};
    }

    PlaceBuilder build_place(Place p) { return PlaceBuilder {*func_, p}; }

    Place project(Place p, Projection proj) {
        auto existing = func_->projections(p.projections);
        std::vector<Projection> new_projs(existing.begin(), existing.end());
        new_projs.push_back(proj);
        return Place {
            .local = p.local, .projections = func_->add_projections(new_projs)
        };
    }

    Place field(Place p, std::uint32_t index) {
        return project(p, {.kind = Projection::Kind::Field, .index = index});
    }

    Place index(Place p, LocalRef idx_local) {
        return project(
            p, {.kind = Projection::Kind::Index, .index = idx_local.index}
        );
    }

    Place deref(Place p) {
        return project(p, {.kind = Projection::Kind::Deref, .index = 0});
    }

    // --- Operands ---
    [[nodiscard]] Operand op(Const c) const { return Operand {c}; }
    [[nodiscard]] Operand op(Place p) const { return Operand {p}; }
    [[nodiscard]] Operand op(LocalRef l) const { return op(place(l)); }

    // --- RValue Factories ---
    [[nodiscard]] static RValue r_use(Operand o) {
        return RValue {RValue::Use {o}};
    }
    [[nodiscard]] static RValue r_unary(UnaryOp op, Operand o) {
        return RValue {RValue::Unary {.operand = o, .op = op}};
    }
    [[nodiscard]] static RValue r_binary(BinaryOp op, Operand l, Operand r) {
        return RValue {RValue::Binary {.left = l, .right = r, .op = op}};
    }
    [[nodiscard]] static RValue r_comp(ComparisonOp op, Operand l, Operand r) {
        return RValue {RValue::Comparison {.left = l, .right = r, .op = op}};
    }
    RValue r_call(Operand callee, std::span<const Operand> args) {
        return RValue {
            RValue::Call {.callee = callee, .args = func_->add_operands(args)}
        };
    }
    [[nodiscard]] static RValue r_ref(Place p) {
        return RValue {RValue::Ref {p}};
    }
    [[nodiscard]] static RValue r_addr_of(Place p) {
        return RValue {RValue::AddressOf {p}};
    }
    [[nodiscard]] static RValue r_cast(Operand o) {
        return RValue {RValue::Cast {o}};
    }
    RValue r_struct(types::TypeId type, std::span<const Operand> args) {
        return RValue {RValue::CreateStruct {
            .type = type, .args = func_->add_operands(args)
        }};
    }
    RValue r_array(std::span<const Operand> items) {
        return RValue {RValue::Array {func_->add_operands(items)}};
    }

    // --- Statements ---
    StatementRef assign(Place p, RValue rv, Location loc = {}) {
        StatementRef ref = func_->add(
            {.data = Statement::Assign {.place = p, .rvalue = rv},
             .location = loc}
        );
        if (current_block_.index != ~0u) {
            func_->block(current_block_).statements.push_back(ref);
        }
        return ref;
    }

    LocalRef assign(types::SpecType type, RValue rv, Location loc = {}) {
        LocalRef local = add_local(type);
        assign(place(local), rv, loc);
        return local;
    }

    LocalRef assign_use(types::SpecType type, Operand op, Location loc = {}) {
        return assign(type, r_use(op), loc);
    }

    LocalRef assign_unary(
        types::SpecType type, UnaryOp op, Operand o, Location loc = {}
    ) {
        return assign(type, r_unary(op, o), loc);
    }

    LocalRef assign_binary(
        types::SpecType type,
        BinaryOp op,
        Operand l,
        Operand r,
        Location loc = {}
    ) {
        return assign(type, r_binary(op, l, r), loc);
    }

    LocalRef assign_comp(
        types::SpecType type,
        ComparisonOp op,
        Operand l,
        Operand r,
        Location loc = {}
    ) {
        return assign(type, r_comp(op, l, r), loc);
    }

    LocalRef assign_call(
        types::SpecType type,
        Operand callee,
        std::span<const Operand> args,
        Location loc = {}
    ) {
        return assign(type, r_call(callee, args), loc);
    }

    LocalRef assign_ref(types::SpecType type, Place p, Location loc = {}) {
        return assign(type, r_ref(p), loc);
    }

    LocalRef assign_addr_of(types::SpecType type, Place p, Location loc = {}) {
        return assign(type, r_addr_of(p), loc);
    }

    LocalRef assign_cast(types::SpecType type, Operand o, Location loc = {}) {
        return assign(type, r_cast(o), loc);
    }

    LocalRef assign_struct(
        types::SpecType type, std::span<const Operand> args, Location loc = {}
    ) {
        return assign(type, r_struct(type.type, args), loc);
    }

    LocalRef assign_array(
        types::SpecType type, std::span<const Operand> items, Location loc = {}
    ) {
        return assign(type, r_array(items), loc);
    }

    StatementRef nop(Location loc = {}) {
        StatementRef ref =
            func_->add({.data = Statement::Nop {}, .location = loc});
        if (current_block_.index != ~0u) {
            func_->block(current_block_).statements.push_back(ref);
        }
        return ref;
    }

    // --- Terminators ---
    void jump(BlockRef target, Location loc = {}) {
        terminate(Terminator::Jump {target}, loc);
    }

    void branch(Operand cond, BlockRef t, BlockRef f, Location loc = {}) {
        terminate(
            Terminator::Branch {
                .condition = cond, .true_target = t, .false_target = f
            },
            loc
        );
    }

    void ret(std::optional<Operand> val = std::nullopt, Location loc = {}) {
        terminate(Terminator::Return {val}, loc);
    }

    void unreachable(Location loc = {}) {
        terminate(Terminator::Unreachable {}, loc);
    }

private:
    void terminate(Terminator::Value data, Location loc) {
        if (current_block_.index != ~0u) {
            func_->block(current_block_).terminator = Terminator {data, loc};
        }
    }

    Func* func_;
    BlockRef current_block_ {~0u};
};

}
