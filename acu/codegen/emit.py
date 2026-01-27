"""
Module for emitting object code from LLVM IR.
This module handles the compilation of LLVM modules to executable machine code.
"""

import sys
from pathlib import Path

import llvmlite.binding as llvm
import llvmlite.ir as llir


def initialize_llvm(opt: int = 0, jit: bool = False) -> llvm.TargetMachine:
    """Initialize LLVM components needed for code generation."""
    # Initialize all required LLVM components
    llvm.initialize_all_targets()
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    llvm.initialize_native_asmparser()
    target = llvm.Target.from_default_triple()
    return target.create_target_machine(
        opt=opt, codemodel="jitdefault" if jit else "default", jit=jit
    )


def optimize(ir_module: llir.Module, tm: llvm.TargetMachine, opt: int = 0):
    ir_module.triple = tm.triple
    ir_module.data_layout = str(tm.target_data)
    print(ir_module)
    ll_module = llvm.parse_assembly(str(ir_module))
    pb = llvm.create_pass_builder(tm, llvm.PipelineTuningOptions(opt))
    pb.getModulePassManager().run(ll_module, pb)
    return ll_module


def emit_object(ll_module: llvm.ModuleRef, tm: llvm.TargetMachine, path: str):
    with open(path, "wb") as f:
        f.write(tm.emit_object(ll_module))


def emit_asm(ll_module: llvm.ModuleRef, tm: llvm.TargetMachine, path: str):
    with open(path, "w") as f:
        f.write(tm.emit_assembly(ll_module))


def emit_ir(ll_module: llvm.ModuleRef, path: str):
    with open(path, "w") as f:
        f.write(str(ll_module))


def emit_bc(ll_module: llvm.ModuleRef, path: str):
    with open(path, "wb") as f:
        f.write(ll_module.as_bitcode())


def emit_exe(
    ll_module: llvm.ModuleRef,
    tm: llvm.TargetMachine,
    path: str,
    object_path: str | None = None,
):
    if object_path is None:
        object_path = str(
            Path(path).with_suffix(".obj" if sys.platform == "win32" else ".o")
        )
        emit_object(ll_module, tm, object_path)

    import setuptools

    _ = setuptools
    from distutils.ccompiler import new_compiler

    compiler = new_compiler()
    output = Path(path)
    compiler.link_executable(
        [object_path],
        output.with_suffix("").name,
        str(output.parent),
        extra_postargs=["-defaultlib:libcmt", "-defaultlib:oldnames"],
    )


def emit_static_lib(
    ll_module: llvm.ModuleRef,
    tm: llvm.TargetMachine,
    path: str,
    object_path: str | None = None,
):
    if object_path is None:
        object_path = str(
            Path(path).with_suffix("obj" if sys.platform == "win32" else "o")
        )
        emit_object(ll_module, tm, object_path)

    import setuptools

    _ = setuptools
    from distutils.ccompiler import new_compiler

    compiler = new_compiler()
    compiler.create_static_lib([object_path], path)


def emit_dynamic_lib(
    ll_module: llvm.ModuleRef,
    tm: llvm.TargetMachine,
    path: str,
    object_path: str | None = None,
):
    if object_path is None:
        object_path = str(
            Path(path).with_suffix("obj" if sys.platform == "win32" else "o")
        )
        emit_object(ll_module, tm, object_path)

    import setuptools

    _ = setuptools
    from distutils.ccompiler import new_compiler

    compiler = new_compiler()
    compiler.link_shared_lib([object_path], path)


emit_shared_lib = emit_dynamic_lib


def emit(
    ll_module: llvm.ModuleRef,
    tm: llvm.TargetMachine,
    llvm_ir_path: str | None = None,
    llvm_bc_path: str | None = None,
    object_path: str | None = None,
    asm_path: str | None = None,
    exe_path: str | None = None,
    static_lib_path: str | None = None,
    dynamic_lib_path: str | None = None,
):
    if llvm_ir_path is not None:
        emit_ir(ll_module, llvm_ir_path)
    if llvm_bc_path is not None:
        emit_bc(ll_module, llvm_bc_path)
    if object_path is not None:
        emit_object(ll_module, tm, object_path)
    if asm_path is not None:
        emit_asm(ll_module, tm, asm_path)
    if exe_path is not None:
        emit_exe(ll_module, tm, exe_path, object_path)
    if static_lib_path is not None:
        emit_static_lib(ll_module, tm, static_lib_path, object_path)
    if dynamic_lib_path is not None:
        emit_dynamic_lib(ll_module, tm, dynamic_lib_path, object_path)
