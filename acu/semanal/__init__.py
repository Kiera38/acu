from acu.errors import CompilationError, ErrorCollector
from acu.parser import nodes
from acu.semanal.basic import add_imports, create_module, convert
from acu.semanal.context import create_context
from acu.semanal.typeanal import TypeAnalyzer
from acu.semanal import ir, types
from acu.source import Source


def type_analyze(
    funcs: list[tuple[ir.Func, Source]], error_collector: ErrorCollector
) -> list[types.TypedFunc]:
    typed_funcs = []
    finished = []
    need_analyze = []
    funcs_to_process = [TypeAnalyzer(func, source) for func, source in funcs]

    while funcs_to_process:
        for f in funcs_to_process:
            if not f.propagate():
                need_analyze.append(f)
            else:
                finished.append(f)
        funcs_to_process = need_analyze
        need_analyze = []

    for f in finished:
        try:
            typed_funcs.append(f.unify())
        except CompilationError as e:
            error_collector.add_error(e)

    return typed_funcs


def analyze(
    modules: list[tuple[nodes.Module, Source]], error_collector: ErrorCollector
) -> tuple[list[ir.Module], list[types.TypedFunc]]:
    ir_modules = [(create_module(module), source) for module, source in modules]
    contexts = {source.name: create_context(module, source) for module, source in ir_modules}
    for module_ast, source in modules:
        add_imports(module_ast, contexts[source.name], contexts)
    funcs = []
    for (module_ast, source), (ir_module, source) in zip(modules, ir_modules):
        convert(module_ast, contexts[source.name])
        funcs.extend([(func, source) for func in ir_module.funcs])

    typed_funcs = type_analyze(funcs, error_collector)
    return [module for module, _ in ir_modules], typed_funcs
