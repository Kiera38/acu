import sys
from argparse import ArgumentParser
from pathlib import Path

from acu import codegen, parser, refanal, semanal
from acu.errors import CompilationError, ErrorCollector
from acu.package import Package
from acu.source import Source


def create_source(file: str):
    path = Path(file)
    return Source(path.name, file, path.read_text())


def is_package_path(path: Path) -> bool:
    """Проверяет, является ли путь папкой пакета (содержит .acu файлы)"""
    return path.is_dir() and bool(list(path.glob("*.acu")))


def parse_args():
    parser = ArgumentParser()
    parser.add_argument("file")

    emit_options = [
        ("llvm_ir", ".ll"),
        ("llvm_bc", ".bc"),
        ("object", ".obj" if sys.platform == "win32" else ".o"),
        ("asm", ".asm"),
        ("exe", ".exe" if sys.platform == "win32" else ""),
        ("static_lib", ".lib" if sys.platform == "win32" else ".a"),
        ("dynamic_lib", ".dll" if sys.platform == "win32" else ".so"),
    ]
    for option, _ in emit_options:
        parser.add_argument(
            f"--{option.replace('_', '-')}",
            nargs="?",
            const="file",
            default=None,
        )
    parser.add_argument(
        "--shared-lib", nargs="?", const="file", default=None, dest="dynamic_lib"
    )

    parser.add_argument("--opt", type=int, choices=[0, 1, 2, 3], default=0)
    args = parser.parse_args()
    file = Path(args.file)
    has_arg = False
    for option, suffix in emit_options:
        val = getattr(args, option)
        if val == "file":
            setattr(args, option, str(file.with_suffix(suffix)))
            has_arg = True
        elif val is not None:
            val_path = Path(val)
            if val_path.suffix != suffix:
                if suffix:
                    print(f"неправильное расширение у файла {val}")
                setattr(args, option, str(file.with_suffix(suffix)))
            has_arg = True
    if not has_arg:
        suffix = ".exe" if sys.platform == "win32" else ""
        args.exe = str(file.with_suffix(suffix))

    return args


def main():
    args = parse_args()
    error_collector = ErrorCollector()
    
    path = Path(args.file)
    
    try:
        # Проверяем, является ли это пакетом (папкой с .acu файлами) или одиночным файлом
        if is_package_path(path):
            # Компиляция пакета
            package = Package(path)
            package.load_modules()
            package.semanal(error_collector)
            package.refanal(error_collector)
            package.codegen(
                llvm_ir_path=args.llvm_ir,
                llvm_bc_path=args.llvm_bc,
                object_path=args.object,
                asm_path=args.asm,
                opt=args.opt,
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
        if args.exe or args.static_lib or args.dynamic_lib and not args.object:
            args.object = str(
                path.with_suffix(".obj" if sys.platform == "win32" else ".o")
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
