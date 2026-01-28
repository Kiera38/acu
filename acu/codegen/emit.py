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


def link_exe(path: str, object_paths: list[str]):
    import setuptools

    _ = setuptools
    from distutils.ccompiler import new_compiler

    compiler = new_compiler()
    output = Path(path)
    compiler.link_executable(
        object_paths,
        output.name,
        str(output.parent),
        extra_postargs=["-defaultlib:libcmt", "-defaultlib:oldnames"],
    )


def link_static_lib(path: str, object_paths: list[str]):
    import setuptools

    _ = setuptools
    from distutils.ccompiler import new_compiler

    compiler = new_compiler()
    output = Path(path)
    compiler.create_static_lib(object_paths, output.name, str(output.parent))


def link_dynamic_lib(path: str, object_paths: list[str]):
    import setuptools

    _ = setuptools
    from distutils.ccompiler import new_compiler

    compiler = new_compiler()
    output = Path(path)
    compiler.link_shared_lib(object_paths, output.name, str(output.parent))


def emit(
    ll_module: llvm.ModuleRef,
    tm: llvm.TargetMachine,
    llvm_ir_path: str | None = None,
    llvm_bc_path: str | None = None,
    object_path: str | None = None,
    asm_path: str | None = None,
):
    if llvm_ir_path is not None:
        emit_ir(ll_module, llvm_ir_path)
    if llvm_bc_path is not None:
        emit_bc(ll_module, llvm_bc_path)
    if object_path is not None:
        emit_object(ll_module, tm, object_path)
    if asm_path is not None:
        emit_asm(ll_module, tm, asm_path)
