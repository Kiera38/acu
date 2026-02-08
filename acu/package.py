"""
Модуль для загрузки и управления системой модулей пакета.
Сканирует папку пакета, загружает все .acu файлы и валидирует импорты.
"""

from dataclasses import dataclass
from pathlib import Path

from acu import codegen, refanal, semanal
from acu.errors import CompilationError, Note
from acu.parser import parse
from acu.parser.nodes import Module, UseStmt, FromUseStmt
from acu.source import Source


@dataclass
class ModuleInfo:
    """Информация о загруженном модуле"""

    source: Source  # Исходный код
    ast: Module  # Распарсенный AST
    imports: set[tuple[str, ...]]  # Множество импортированных модулей


class Package:
    def __init__(self, path: Path):
        """Инициализация пакета из папки"""
        if not path.is_dir():
            raise ValueError(f"Package path must be a directory: {path}")
        self.path = path
        self.modules: dict[str, ModuleInfo] = {}
        self.ir_modules: list[semanal.ir.Module] = []
        self.funcs = []

    def load_modules(self, path: Path | None = None, package_name: str = '') -> None:
        """Загружает все модули пакета из папки"""
        if path is None:
            path = self.path

        for file_path in path.glob("*.acu"):
            module_name = file_path.stem  # Имя без расширения
            if package_name:
                module_name = '.'.join((package_name, module_name))
            code = file_path.read_text()
            source = Source(module_name, str(file_path), code)
            ast = parse(source)
            imports = {tuple(import_stmt.module_name) for import_stmt in ast.imports}
            module_info = ModuleInfo(source=source, ast=ast, imports=imports)
            self.modules[module_name] = module_info
        
        for dir in path.iterdir():
            if dir.is_dir():
                name = dir.stem
                if package_name:
                    name = '.'.join((package_name, name))
                self.load_modules(dir, name)

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

    def codegen(
        self,
        llvm_ir_path: str | None = None,
        llvm_bc_path: str | None = None,
        object_path: str | None = None,
        asm_path: str | None = None,
        opt: int = 0,
    ) -> None:
        codegen.emit_files(
            self.ir_funcs,
            llvm_ir_path,
            llvm_bc_path,
            object_path,
            asm_path,
            opt,
        )
