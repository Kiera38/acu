#include "refanal/optimizer.h"

#include <map>
#include <vector>


namespace acu::refanal {

namespace {

struct Optimizer {
    ir::Func* func;
    std::vector<ir::InstRef> replacements;

    Optimizer(ir::Func& f) : func(&f) {
        replacements.resize(func->insts().size());
        for (uint32_t i = 0; i < replacements.size(); ++i) {
            replacements[i] = {i};
        }
    }

    ir::InstRef get_rep(ir::InstRef ref) {
        if (ref.index == ~0u) return ref;
        uint32_t curr = ref.index;
        while (replacements[curr].index != curr) {
            curr = replacements[curr].index;
        }
        return {curr};
    }

    void copy_prop() {
        for (uint32_t b_idx = 0; b_idx < func->blocks().size(); ++b_idx) {
            auto& block = func->block(ir::BlockRef {b_idx});
            std::map<uint32_t, ir::InstRef> var_values;

            for (auto inst_ref : block.insts) {
                auto& inst = func->inst(inst_ref);

                inst.data.visit(
                    [&](ir::Inst::LoadVar& i) {
                        i.var = get_rep(i.var);
                        auto it = var_values.find(i.var.index);
                        if (it != var_values.end()) {
                            replacements[inst_ref.index] = it->second;
                        }
                    },
                    [&](ir::Inst::Store& i) {
                        i.var = get_rep(i.var);
                        i.value = get_rep(i.value);
                        var_values[i.var.index] = i.value;
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
                            replacements[inst_ref.index] = i.value;
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
                        var_values.erase(i.var.index);
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
                        var_values.erase(i.var.index);
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

        // Remove redundant instructions (only local)
        for (uint32_t b_idx = 0; b_idx < func->blocks().size(); ++b_idx) {
            auto& block = func->block(ir::BlockRef {b_idx});
            std::vector<ir::InstRef> new_insts;
            for (auto inst_ref : block.insts) {
                if (replacements[inst_ref.index].index != inst_ref.index) {
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
            std::vector<uint32_t> preds(func->blocks().size(), 0);
            if (!func->blocks().empty()) preds[0] = 1;

            for (uint32_t i = 0; i < func->blocks().size(); ++i) {
                const auto& block = func->block(ir::BlockRef {i});
                if (block.insts.empty()) continue;
                const auto& last = func->inst(block.insts.back());
                last.data.visit(
                    [&](const ir::Inst::Jump& j) { preds[j.target.index]++; },
                    [&](const ir::Inst::Branch& b) {
                        preds[b.true_target.index]++;
                        preds[b.false_target.index]++;
                    },
                    [&](auto&) {}
                );
            }

            for (uint32_t i = 0; i < func->blocks().size(); ++i) {
                auto& block = func->block(ir::BlockRef {i});
                if (block.insts.empty()) continue;
                auto& last = func->inst(block.insts.back());
                if (auto* j = last.data.get_if<ir::Inst::Jump>()) {
                    uint32_t target_idx = j->target.index;
                    if (target_idx != i && preds[target_idx] == 1) {
                        auto& target_block = func->block(j->target);
                        block.insts.pop_back();  // remove Jump
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
        func->replace_blocks(std::move(new_blocks));
    }

    void process() {
        copy_prop();
        merge_blocks();
        compact_blocks();
    }
};

}  // namespace

void optimize(ir::Module& module) {
    for (auto& func : module.funcs()) {
        Optimizer opt(func);
        opt.process();
    }
}

}  // namespace acu::refanal
