from acu.errors import CompilationError, ErrorCollector
from acu.parser import nodes
from acu.semanal.basic import convert_module
from acu.semanal.typeanal import TypeAnalyzer
from acu.source import Source


def analyze(module: nodes.Module, source: Source, error_collector: ErrorCollector):
    ir_module = convert_module(module, source)
    funcs = [TypeAnalyzer(func, source) for func in ir_module.funcs]
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

    typed_funcs = []
    for func in finished:
        try:
            typed_funcs.append(func.unify())
        except CompilationError as e:
            error_collector.add_error(e)

    return ir_module, typed_funcs
