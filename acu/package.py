"""
Модуль для загрузки и управления системой модулей пакета.
Сканирует папку пакета, загружает все .acu файлы и валидирует импорты.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from acu import codegen, refanal, semanal
from acu.errors import CompilationError, Note
from acu.parser import parse
from acu.parser.nodes import Module, UseStmt, FromUseStmt
from acu.source import Source


@dataclass
class CodegenParams:
    output_path: Path
    llvm_ir: bool
    llvm_bc: bool
    object: bool
    asm: bool
    opt: int


@dataclass
class ModuleInfo:
    """Информация о загруженном модуле"""

    source: Source  # Исходный код
    ast: Module  # Распарсенный AST
    imports: set[tuple[str, ...]]  # Множество импортированных модулей


class Package:
    def __init__(self, path: Path, name: str = ""):
        """Инициализация пакета из папки"""
        if not path.is_dir():
            raise ValueError(f"Package path must be a directory: {path}")
        self.path = path
        self.name = name
        self.modules: dict[str, ModuleInfo] = {}
        self.ir_modules: list[semanal.ir.Module] = []
        self.funcs = []
        self.subpackages: dict[str, Package] = {}

    def load_modules(self, project: Project) -> None:
        """Загружает все модули пакета из папки"""
        root_module_path = self.path / "package.acu"
        if root_module_path.exists():
            self._load_module(root_module_path, self.name)

        for file_path in self.path.glob("*.acu"):
            module_name = file_path.stem  # Имя без расширения
            if module_name == "package":
                continue
            if self.name:
                module_name = ".".join((self.name, module_name))
            self._load_module(file_path, module_name)

        for dir in self.path.iterdir():
            if dir.is_dir():
                name = dir.stem
                if self.name:
                    name = ".".join((self.name, name))
                package = self.subpackages[name] = Package(dir, name)
                project.packages.append(package)
                package.load_modules(project)

    def _load_module(self, file_path: Path, module_name: str):
        code = file_path.read_text()
        source = Source(module_name, str(file_path), code)
        ast = parse(source)
        imports = {tuple(import_stmt.module_name) for import_stmt in ast.imports}
        module_info = ModuleInfo(source=source, ast=ast, imports=imports)
        self.modules[module_name] = module_info

    def _validate_imports(self) -> None:
        """
        Валидирует, что все импортированные модули существуют.

        Raises:
            CompilationError: Если импортированный модуль не найден
        """
        # todo: меня как-то смущает этот код
        for module_info in self.modules.values():
            for imported_module in module_info.imports:
                if imported_module not in self.modules:
                    # Найти первый импорт этого модуля для сообщения об ошибке
                    for import_stmt in module_info.ast.imports:
                        if isinstance(import_stmt, (UseStmt, FromUseStmt)):
                            if import_stmt.module_name == imported_module:
                                raise CompilationError(
                                    import_stmt.location,
                                    f"Module '{imported_module}' not found in package",
                                    module_info.source,
                                    helps=[
                                        Note(
                                            f"Available modules: {', '.join(sorted(self.modules.keys()))}"
                                        )
                                    ],
                                )

    def get_module(self, name: str) -> ModuleInfo:
        """Получить информацию о модуле по имени"""
        if name not in self.modules:
            raise ValueError(f"Module '{name}' not found")
        return self.modules[name]

    def semanal(self, error_collector) -> None:
        self._validate_imports()
        self.ir_modules, self.funcs = semanal.analyze(
            [
                (module_info.ast, module_info.source)
                for module_info in self.modules.values()
            ],
            error_collector,
        )

    def refanal(self, error_collector) -> None:
        self.ir_funcs = refanal.analyze(self.funcs, error_collector)

    def codegen(self, params: CodegenParams) -> None:
        codegen.emit_files(
            self.ir_funcs,
            str(self._get_file(params.output_path, ".ll")) if params.llvm_ir else None,
            str(self._get_file(params.output_path, ".bc")) if params.llvm_bc else None,
            str(self._get_file(params.output_path, ".obj")) if params.object else None,
            str(self._get_file(params.output_path, ".asm")) if params.asm else None,
            params.opt,
        )

    def _get_file(self, path: Path, suffix: str):
        if not self.name:
            return self.path / f"package{suffix}"
        names = self.name.split(".")
        return path.joinpath(*names).with_suffix(suffix)


class Project:
    def __init__(self) -> None:
        self.packages: list[Package] = []

    def compile(
        self, error_collector, path: Path, codegen_params: CodegenParams, name: str = ""
    ):
        self.find_packages(path, name)
        for package in self.packages:
            package.semanal(error_collector)
        for package in self.packages:
            package.refanal(error_collector)
        for package in self.packages:
            package.codegen(codegen_params)

    def find_packages(self, path: Path, name: str = ""):
        package = Package(path, name)
        self.packages.append(package)
        package.load_modules(self)
