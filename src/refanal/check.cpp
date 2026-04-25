#include "refanal/check.h"
#include "refanal/dataflow.h"
#include <map>

namespace acu::refanal {

enum class StorageBase : uint8_t {
    None = 0,
    Local = 1 << 0,
    Param = 1 << 1,
    Global = 1 << 2,
    Heap = 1 << 3,
    Unknown = 0xFF
};

inline StorageBase operator|(StorageBase a, StorageBase b) {
    return static_cast<StorageBase>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

struct ReferenceState {
    StorageBase base = StorageBase::None;

    bool operator==(const ReferenceState& other) const = default;

    static ReferenceState meet(const ReferenceState& a, const ReferenceState& b) {
        return {a.base | b.base};
    }
};

struct BlockState {
    std::vector<ReferenceState> var_states;

    bool operator==(const BlockState& other) const {
        return var_states == other.var_states;
    }

    static BlockState bottom() { return {}; }

    static BlockState entry(ir::Func& func, size_t var_count) {
        BlockState state;
        state.var_states.resize(var_count, {StorageBase::None});
        return state;
    }

    static BlockState meet(const BlockState& a, const BlockState& b) {
        if (a.var_states.empty()) return b;
        if (b.var_states.empty()) return a;

        BlockState result;
        size_t size = a.var_states.size();
        result.var_states.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            result.var_states.push_back(ReferenceState::meet(a.var_states[i], b.var_states[i]));
        }
        return result;
    }
};

class ReferenceChecker {
    ir::Func* func_;
    ::acu::ir::AnalyzedPackage* analyzed_;
    ErrorHandler* err_handler_;
    std::vector<ir::InstRef> var_decls_;
    std::map<ir::InstRef, size_t> var_to_idx_;
    IndexVector<ReferenceState, ir::InstRef> inst_states_;

public:
    ReferenceChecker(ir::Func& func, ::acu::ir::AnalyzedPackage& analyzed, ErrorHandler& err_handler)
        : func_(&func), analyzed_(&analyzed), err_handler_(&err_handler) {
        for (auto i : func.insts().indices()) {
            if (func.inst(i).data.is<ir::Inst::VarDecl>()) {
                var_to_idx_[i] = var_decls_.size();
                var_decls_.push_back(i);
            }
        }
        inst_states_.resize(func.insts().size(), {StorageBase::None});
    }

    void check() {
        IndexVector<BlockState, ir::BlockRef> in, out;
        
        auto transfer_func = [this](ir::BlockRef br, const BlockState& state) {
            return transfer(br, state);
        };

        // solve_forward expects LatticeElement::entry(func)
        // but we need var_count. I'll manually adapt solve_forward or use a wrapper.
        
        in.clear();
        out.clear();
        for (auto i : func_->blocks().indices()) {
            in.push_back(BlockState::bottom());
            out.push_back(BlockState::bottom());
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (auto i : func_->blocks().indices()) {
                auto& block = func_->block(i);

                BlockState new_in = BlockState::bottom();
                if (i.index == 0) {
                    new_in = BlockState::entry(*func_, var_decls_.size());
                }

                for (auto pred : block.preds) {
                    new_in = BlockState::meet(new_in, out[pred]);
                }

                if (new_in != in[i]) {
                    in[i] = new_in;
                    changed = true;
                }

                BlockState new_out = transfer(i, in[i]);
                if (new_out != out[i]) {
                    out[i] = new_out;
                    changed = true;
                }
            }
        }

        // Final verification pass
        for (auto i : func_->blocks().indices()) {
            verify_block(i, in[i]);
        }
    }

private:
    BlockState transfer(ir::BlockRef br, BlockState state) {
        auto& block = func_->block(br);
        for (auto ir_ref : block.insts) {
            const auto& inst = func_->inst(ir_ref);
            ReferenceState result {StorageBase::None};

            inst.data.visit(
                [&](const ir::Inst::VarDecl&) {
                    result = {StorageBase::Local};
                },
                [&](const ir::Inst::LoadParam&) {
                    result = {StorageBase::None};
                },
                [&](const ir::Inst::LoadVar& data) {
                    auto it = var_to_idx_.find(data.var);
                    if (it != var_to_idx_.end()) {
                        result = state.var_states[it->second];
                    }
                },
                [&](const ir::Inst::Store& data) {
                    auto it = var_to_idx_.find(data.var);
                    if (it != var_to_idx_.end()) {
                        state.var_states[it->second] = inst_states_[data.value];
                    }
                },
                [&](const ir::Inst::AddressOf& data) {
                    const auto& val_inst = func_->inst(data.value);
                    if (val_inst.type.specifier == types::Specifier::Val ||
                        val_inst.type.specifier == types::Specifier::None) {
                        result = {StorageBase::Local};
                    } else {
                        result = inst_states_[data.value];
                    }
                },
                [&](const ir::Inst::CreateStruct& data) {
                    for (auto arg : func_->inst_refs(data.args)) {
                        result = ReferenceState::meet(result, inst_states_[arg]);
                    }
                },
                [&](const ir::Inst::GetField& data) {
                    result = inst_states_[data.value];
                },
                [&](const ir::Inst::SetField& data) {
                    auto it = var_to_idx_.find(data.var);
                    if (it != var_to_idx_.end()) {
                        state.var_states[it->second] =
                            ReferenceState::meet(state.var_states[it->second], inst_states_[data.value]);
                    }
                },
                [&](const ir::Inst::Array& data) {
                    for (auto item : func_->inst_refs(data.items)) {
                        result = ReferenceState::meet(result, inst_states_[item]);
                    }
                },
                [&](const ir::Inst::GetItem& data) {
                    result = inst_states_[data.value];
                },
                [&](const ir::Inst::SetItem& data) {
                    auto it = var_to_idx_.find(data.var);
                    if (it != var_to_idx_.end()) {
                        state.var_states[it->second] =
                            ReferenceState::meet(state.var_states[it->second], inst_states_[data.value]);
                    }
                },
                [&](const ir::Inst::Call&) {
                    if (inst.type.specifier == types::Specifier::Let ||
                        inst.type.specifier == types::Specifier::Var) {
                        // Assume external origin for references returned by calls
                        result = {StorageBase::Param | StorageBase::Global | StorageBase::Heap};
                    }
                },
                [&](const ir::Inst::Deref&) {
                    // Dereference returns a value, so base is None
                    result = {StorageBase::None};
                },
                [&](const ir::Inst::Cast& data) {
                    result = inst_states_[data.value];
                },
                [&](const ir::Inst::Const&) {
                    result = {StorageBase::None};
                },
                [&](const ir::Inst::Binary&) {
                    if (inst.type.specifier == types::Specifier::Let ||
                        inst.type.specifier == types::Specifier::Var) {
                        result = {StorageBase::Local};
                    }
                },
                [&](const auto&) {
                    // Other instructions generally return values or are terminators
                    result = {StorageBase::None};
                }
            );
            inst_states_[ir_ref] = result;
        }
        return state;
    }

    void verify_block(ir::BlockRef br, BlockState state) {
        auto& block = func_->block(br);
        for (auto ir_ref : block.insts) {
            const auto& inst = func_->inst(ir_ref);
            
            inst.data.visit(
                [&](const ir::Inst::Return& data) {
                    if (data.value) {
                        auto res_state = inst_states_[*data.value];
                        StorageBase base = res_state.base;

                        // If return type is a reference, we also check if the value was loaded 
                        // from a local variable (which is a leak of that variable's storage)
                        if (func_->return_type().specifier == types::Specifier::Let ||
                            func_->return_type().specifier == types::Specifier::Var) {
                            
                            const auto& val_inst = func_->inst(*data.value);
                            if (val_inst.type.specifier == types::Specifier::Val ||
                                val_inst.type.specifier == types::Specifier::None) {
                                base = base | StorageBase::Local;
                            } else if (auto load = val_inst.data.get_if<ir::Inst::LoadVar>()) {
                                const auto& var_inst = func_->inst(load->var);
                                if (var_inst.type.specifier == types::Specifier::Val ||
                                    var_inst.type.specifier == types::Specifier::None) {
                                    auto var_state = inst_states_[load->var];
                                    if (static_cast<uint8_t>(var_state.base) &
                                        static_cast<uint8_t>(StorageBase::Local)) {
                                        base = base | StorageBase::Local;
                                    }
                                }
                            }
                        }

                        if (static_cast<uint8_t>(base) & static_cast<uint8_t>(StorageBase::Local)) {
                            err_handler_->error(func_->source(), inst.location, "returning reference to local value");
                        }
                    }
                },
                [&](const ir::Inst::Store& data) {
                    auto& var_inst = func_->inst(data.var);
                    if (var_inst.type.specifier == types::Specifier::Let) {
                        // Actually, initialization store is allowed.
                        // For now, let's keep it simple. Correctness of 'let' initialization 
                        // is better handled in semanal.
                    }
                },
                [&](const auto&) {}
            );

            // Re-run transfer logic to update state for following instructions in the block
            // (inst_states_ is already filled, but we might need to update state.var_states)
            inst.data.visit(
                [&](const ir::Inst::Store& data) {
                    auto it = var_to_idx_.find(data.var);
                    if (it != var_to_idx_.end()) {
                        state.var_states[it->second] = inst_states_[data.value];
                    }
                },
                [&](const auto&) {}
            );
        }
    }
};

void check(
    ir::Module& module,
    ::acu::ir::AnalyzedPackage& analyzed,
    ErrorHandler& err_handler
) {
    for (auto i : module.funcs().indices()) {
        auto& func = module.funcs()[i];
        ReferenceChecker checker(func, analyzed, err_handler);
        checker.check();
    }
}

}
