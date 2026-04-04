#pragma once

#include <concepts>
#include <vector>

#include "refanal/ir.h"

namespace acu::refanal::dataflow {

template <typename T>
concept BaseLattice = requires(T left, T right) {
    { T::bottom() } -> std::same_as<T>;
    { T::meet(left, right) } -> std::same_as<T>;
};

template <typename T>
concept ForwardLattice = BaseLattice<T> && requires(ir::Func& func) {
    { T::entry(func) } -> std::same_as<T>;
};

template <typename T>
concept BackwardLattice =
    BaseLattice<T> && requires(ir::Func& func, ir::BlockRef ref) {
        { T::exit(func, ref) } -> std::same_as<T>;
    };

template <typename T, typename LatticeElement>
concept Transfer = requires(T transfer, ir::BlockRef ref, LatticeElement in) {
    { transfer(ref, in) } -> std::same_as<LatticeElement>;
};

template <ForwardLattice LatticeElement, Transfer<LatticeElement> TransferFunc>
void solve_forward(
    ir::Func& func,
    IndexVector<LatticeElement, ir::BlockRef>& in,
    IndexVector<LatticeElement, ir::BlockRef>& out,
    TransferFunc transfer
) {
    in.clear();
    out.clear();
    for (auto i : func.blocks().indices()) {
        in.push_back(LatticeElement::bottom());
        out.push_back(LatticeElement::bottom());
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto i : func.blocks().indices()) {
            auto& block = func.block(i);

            LatticeElement new_in = LatticeElement::bottom();
            if (i.index == 0) {
                new_in = LatticeElement::entry(func);
            }

            for (auto pred : block.preds) {
                new_in = LatticeElement::meet(new_in, out[pred]);
            }

            if (new_in != in[i]) {
                in[i] = new_in;
                changed = true;
            }

            LatticeElement new_out = transfer(i, in[i]);
            if (new_out != out[i]) {
                out[i] = new_out;
                changed = true;
            }
        }
    }
}

template <BackwardLattice LatticeElement, Transfer<LatticeElement> TransferFunc>
void solve_backward(
    ir::Func& func,
    IndexVector<LatticeElement, ir::BlockRef>& in,
    IndexVector<LatticeElement, ir::BlockRef>& out,
    TransferFunc transfer
) {
    in.clear();
    out.clear();
    for (auto i : func.blocks().indices()) {
        in.push_back(LatticeElement::bottom());
        out.push_back(LatticeElement::bottom());
    }

    bool changed = true;
    while (changed) {
        changed = false;
        auto blocks = func.blocks();
        for (auto it = blocks.index_end(); it != blocks.index_begin();) {
            --it;
            auto i = *it;
            auto& block = func.block(i);

            LatticeElement new_out = LatticeElement::bottom();
            if (block.succs.empty()) {
                new_out = LatticeElement::exit(func, i);
            }

            for (auto succ : block.succs) {
                new_out = LatticeElement::meet(new_out, in[succ]);
            }

            if (new_out != out[i]) {
                out[i] = new_out;
                changed = true;
            }

            LatticeElement new_in = transfer(i, out[i]);
            if (new_in != in[i]) {
                in[i] = new_in;
                changed = true;
            }
        }
    }
}

}  // namespace acu::refanal::dataflow
