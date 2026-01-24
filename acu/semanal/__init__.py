from acu.parser import nodes
from acu.semanal.basic import convert_module
from acu.semanal.typeanal import TypeAnalyzer


def analyze(module: nodes.Module):
    ir_module = convert_module(module)
    funcs = [TypeAnalyzer(func) for func in ir_module.funcs]
    finished = []
    need_analyze = []
    while funcs:
        for func in funcs:
            if not func.propagate():
                need_analyze.append(func)
            else:
                finished.append(func)
        funcs = need_analyze
        need_analyze = []
    return ir_module, [func.unify() for func in finished]