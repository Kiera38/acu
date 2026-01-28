import llvmlite

llvmlite.ir_layer_typed_pointers_enabled = False

from acu.codegen import emit
from acu.codegen.generator import generate_llvm_ir
from acu.refanal.flow_graph_ir import FuncIR


def emit_files(
    funcs: list[FuncIR],
    llvm_ir_path: str | None = None,
    llvm_bc_path: str | None = None,
    object_path: str | None = None,
    asm_path: str | None = None,
    opt: int = 0,
):
    tm = emit.initialize_llvm(opt)
    ir_module = generate_llvm_ir(funcs)
    ll_module = emit.optimize(ir_module, tm, opt)
    emit.emit(ll_module, tm, llvm_ir_path, llvm_bc_path, object_path, asm_path)


def link(
    object_paths: list[str],
    exe_path: str | None = None,
    static_lib_path: str | None = None,
    dynamic_lib_path: str | None = None,
):
    if static_lib_path is not None:
        emit.link_static_lib(static_lib_path, object_paths)
    if dynamic_lib_path is not None:
        emit.link_dynamic_lib(dynamic_lib_path, object_paths)
    if exe_path is not None:
        emit.link_exe(exe_path, object_paths)
