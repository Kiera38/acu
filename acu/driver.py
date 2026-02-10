import sys
from argparse import ArgumentParser
from pathlib import Path

from acu import codegen, parser, refanal, semanal
from acu.errors import CompilationError, ErrorCollector
from acu.package import CodegenParams, Project
from acu.source import Source


def create_source(file: str):
    path = Path(file)
    return Source(path.name, file, path.read_text())


def is_package_path(path: Path) -> bool:
    """Проверяет, является ли путь папкой пакета (содержит .acu файлы)"""
    return path.is_dir()


def parse_args():
    parser = ArgumentParser()
    parser.add_argument("file")
    parser.add_argument("-o", "--output")

    emit_options = [
        "llvm_ir",
        "llvm_bc",
        "object",
        "asm",
        "exe",
        "static_lib",
        "dynamic_lib",
    ]
    for option in emit_options:
        parser.add_argument(f"--{option.replace('_', '-')}", nargs="?")
    parser.add_argument("--shared-lib", nargs="?", dest="dynamic_lib")

    parser.add_argument("--opt", type=int, choices=[0, 1, 2, 3], default=0)
    args = parser.parse_args()
    has_arg = any(getattr(args, option) for option in emit_options)
    if not has_arg:
        args.exe = True
    if not args.output:
        path = Path(args.file)
        if path.is_dir():
            args.output = str(path)
        else:
            args.output = str(path.parent)
    return args


def get_codegen_params(args):
    if args.exe or args.static_lib or args.dynamic_lib:
        args.object = True
    return CodegenParams(
        Path(args.output), args.llvm_ir, args.llvm_bc, args.object, args.asm, args.opt
    )


def main():
    args = parse_args()
    error_collector = ErrorCollector()

    path = Path(args.file)

    try:
        # Проверяем, является ли это пакетом (папкой с .acu файлами) или одиночным файлом
        if is_package_path(path):
            # Компиляция пакета
            project = Project()
            project.compile(error_collector, path, get_codegen_params(args))
            output = Path(args.output)
            codegen.link(
                [str(o) for o in output.glob("**/*.obj")],
                str(output / "output.exe") if args.exe else None,
                str(output / "output.lib") if args.static_lib else None,
                str(output / "output.dll") if args.dynamic_lib else None,
            )
        else:
            # Компиляция одного файла (старый режим)
            source = create_source(args.file)
            ast = parser.parse(source)
            ir, funcs = semanal.analyze([(ast, source)], error_collector)
            fg_ir = refanal.analyze(funcs, error_collector)
            codegen.emit_files(
                fg_ir,
                llvm_ir_path=args.llvm_ir,
                llvm_bc_path=args.llvm_bc,
                object_path=args.object,
                asm_path=args.asm,
                opt=args.opt,
            )

            codegen.link(
                [args.object],
                exe_path=args.exe,
                static_lib_path=args.static_lib,
                dynamic_lib_path=args.dynamic_lib,
            )
    except CompilationError as e:
        error_collector.add_error(e)

    # Вывести все ошибки если они есть
    if error_collector.has_errors():
        error_collector.report_errors()
        sys.exit(1)
