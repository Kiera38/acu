from acu.refanal.build_ir import build_module
from acu.refanal.copy_propagation import do_copy_propagation
from acu.refanal.dataflow import cleanup_cfg
from acu.refanal.flag_elimination import do_flag_elimination
from acu.refanal.flow_graph_ir import FuncIR
from acu.refanal.lower_refs import lower_refs
from acu.refanal.refanal import analyze as ref_spec_analyze
from acu.semanal.types import TypedFunc


def analyze(funcs: list[TypedFunc]) -> list[FuncIR]:
    ir_funcs = build_module(funcs)
    for fn in ir_funcs:
        cleanup_cfg(fn.blocks)
        ref_spec_analyze(fn)
        do_copy_propagation(fn)
        # do_flag_elimination(fn)
        lower_refs(fn)
    return ir_funcs
