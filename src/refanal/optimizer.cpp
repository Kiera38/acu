#include "refanal/optimizer.h"

#include <map>
#include <vector>



namespace acu::refanal {
namespace {

struct Optimizer {
    ir::Func* func;
    IndexVector<ir::InstRef, ir::InstRef> replacements;

    Optimizer(ir::Func& f) : func(&f) {
        replacements.resize(func->insts().size());
        for (auto i : func->insts().indices()) {
            replacements[i] = i;
        }
    }

    ir::InstRef get_rep(ir::InstRef ref) {
        if (ref.index == ~0u) return ref;
        auto curr = ref;
        while (replacements[curr] != curr) {
            curr = replacements[curr];
        }
        return {curr};
    }

    void copy_prop() {
        for (auto b_idx : func->blocks().indices()) {
            auto& block = func->block(b_idx);
            std::map<ir::InstRef, ir::InstRef> var_values;

            for (auto inst_ref : block.insts) {
                auto& inst = func->inst(inst_ref);

                inst.data.visit(
                    [&](ir::Inst::LoadVar& i) {
                        i.var = get_rep(i.var);
                        auto it = var_values.find(i.var);
                        if (it != var_values.end()) {
                            replacements[inst_ref] = it->second;
                        }
                    },
                    [&](ir::Inst::Store& i) {
                        i.var = get_rep(i.var);
                        i.value = get_rep(i.value);
                        var_values[i.var] = i.value;
                    },
                    [&](ir::Inst::Binary& i) {
                        i.left = get_rep(i.left);
                        i.right = get_rep(i.right);
                    },
                    [&](ir::Inst::Unary& i) { i.value = get_rep(i.value); },
                    [&](ir::Inst::Comparison& i) {
                        i.left = get_rep(i.left);
                        i.right = get_rep(i.right);
                    },
                    [&](ir::Inst::Call& i) {
                        i.value = get_rep(i.value);
                        for (auto& arg : func->inst_refs(i.args))
                            arg = get_rep(arg);
                    },
                    [&](ir::Inst::Cast& i) {
                        i.value = get_rep(i.value);
                        if (func->inst(i.value).type == inst.type) {
                            replacements[inst_ref] = i.value;
                        }
                    },
                    [&](ir::Inst::CreateStruct& i) {
                        for (auto& arg : func->inst_refs(i.args))
                            arg = get_rep(arg);
                    },
                    [&](ir::Inst::GetField& i) { i.value = get_rep(i.value); },
                    [&](ir::Inst::SetField& i) {
                        i.var = get_rep(i.var);
                        i.value = get_rep(i.value);
                        var_values.erase(i.var);
                    },
                    [&](ir::Inst::AddressOf& i) { i.value = get_rep(i.value); },
                    [&](ir::Inst::GetItem& i) {
                        i.value = get_rep(i.value);
                        i.index = get_rep(i.index);
                    },
                    [&](ir::Inst::SetItem& i) {
                        i.var = get_rep(i.var);
                        i.index = get_rep(i.index);
                        i.value = get_rep(i.value);
                        var_values.erase(i.var);
                    },
                    [&](ir::Inst::Deref& i) { i.value = get_rep(i.value); },
                    [&](ir::Inst::Array& i) {
                        for (auto& item : func->inst_refs(i.items))
                            item = get_rep(item);
                    },
                    [&](ir::Inst::Branch& i) {
                        i.condition = get_rep(i.condition);
                    },
                    [&](ir::Inst::Return& i) {
                        if (i.value) *i.value = get_rep(*i.value);
                    },
                    [&](auto&) {}
                );
            }
        }

        // Remove redundant instructions
        for (auto b_idx : func->blocks().indices()) {
            auto& block = func->block(b_idx);
            std::vector<ir::InstRef> new_insts;
            for (auto inst_ref : block.insts) {
                if (replacements[inst_ref] != inst_ref) {
                    auto& inst = func->inst(inst_ref);
                    if (inst.data.is<ir::Inst::LoadVar>() ||
                        inst.data.is<ir::Inst::Cast>()) {
                        continue;
                    }
                }
                new_insts.push_back(inst_ref);
            }
            block.insts = std::move(new_insts);
        }
    }

    void merge_blocks() {
        bool changed = true;
        while (changed) {
            changed = false;
            func->rebuild_cfg();

            for (auto i : func->blocks().indices()) {
                auto& block = func->block(i);
                if (block.insts.empty()) continue;
                auto& last = func->inst(block.insts.back());
                if (auto* j = last.data.get_if<ir::Inst::Jump>()) {
                    if (j->target != i && func->block(j->target).preds.size() == 1) {
                        auto& target_block = func->block(j->target);
                        block.insts.pop_back();
                        block.insts.insert(
                            block.insts.end(),
                            target_block.insts.begin(),
                            target_block.insts.end()
                        );
                        target_block.insts.clear();
                        changed = true;
                    }
                }
            }
        }
    }

    void compact_blocks() {
        if (func->blocks().empty()) return;

        std::vector<int> old_to_new(func->blocks().size(), -1);
        std::vector<uint32_t> queue;

        queue.push_back(0);
        old_to_new[0] = 0;

        uint32_t head = 0;
        while (head < queue.size()) {
            uint32_t curr_idx = queue[head++];
            const auto& block = func->block(ir::BlockRef {curr_idx});
            if (block.insts.empty()) continue;
            const auto& last = func->inst(block.insts.back());

            auto add_reachable = [&](ir::BlockRef br) {
                if (old_to_new[br.index] == -1) {
                    old_to_new[br.index] = (int)queue.size();
                    queue.push_back(br.index);
                }
            };

            last.data.visit(
                [&](const ir::Inst::Jump& j) { add_reachable(j.target); },
                [&](const ir::Inst::Branch& b) {
                    add_reachable(b.true_target);
                    add_reachable(b.false_target);
                },
                [&](auto&) {}
            );
        }

        std::vector<ir::Block> new_blocks;
        for (uint32_t old_idx : queue) {
            auto& block = func->block(ir::BlockRef {old_idx});
            for (auto inst_ref : block.insts) {
                auto& inst = func->inst(inst_ref);
                inst.data.visit(
                    [&](ir::Inst::Jump& j) {
                        j.target.index = (uint32_t)old_to_new[j.target.index];
                    },
                    [&](ir::Inst::Branch& b) {
                        b.true_target.index =
                            (uint32_t)old_to_new[b.true_target.index];
                        b.false_target.index =
                            (uint32_t)old_to_new[b.false_target.index];
                    },
                    [&](auto&) {}
                );
            }
            new_blocks.push_back(std::move(block));
        }
        func->replace_blocks(new_blocks);
    }

    void process() {
        copy_prop();
        merge_blocks();
        compact_blocks();
    }
};

}  // namespace

void optimize(
    ir::Module& module,
    acu::ir::AnalyzedPackage& analyzed,
    ErrorHandler& err_handler
) {
    // infer_specifiers(module, err_handler); // Removed, now handled in semanal
    for (auto i : module.funcs().indices()) {
        Optimizer opt(module.funcs()[i]);
        opt.process();
    }
}

}  // namespace acu::refanal
