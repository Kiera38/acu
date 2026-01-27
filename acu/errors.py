import sys
from dataclasses import dataclass, field
from typing import List

from acu.source import Location, Source


class CompilationError(Exception):
    """Базовый класс для всех ошибок компиляции"""

    def __init__(self, location: Location, message: str, source: Source | None = None):
        self.location = location
        self.message = message
        self.source = source
        super().__init__(self.format_message())

    def format_message(self) -> str:
        if self.source is None:
            return f"Error at line {self.location.line}, column {self.location.column}: {self.message}"

        lines = self.source.lines
        line_idx = self.location.line - 1  # assuming 1-based
        if 0 <= line_idx < len(lines):
            line = lines[line_idx]
            pointer = " " * (self.location.column - 1) + "^"
            return f"Error in {self.source.name}:{self.location.line}:{self.location.column}:\n{line}\n{pointer}\n{self.message}"
        else:
            return f"Error in {self.source.name}:{self.location.line}:{self.location.column}: {self.message}"


class SyntaxError(CompilationError):
    """Ошибка синтаксического анализа"""

    pass


class SemanticError(CompilationError):
    """Ошибка семантического анализа"""

    pass


class TypeError(CompilationError):
    """Ошибка типа"""

    pass


class NameError(CompilationError):
    """Ошибка имени (переменная/функция не найдена)"""

    pass


class ValidationError(CompilationError):
    """Ошибка валидации"""

    pass


class ErrorCollector:
    """Сборщик ошибок для агрегирования и управления ошибками компиляции"""

    def __init__(self):
        self.errors: List[CompilationError] = []
        self.max_errors = 10  # Максимальное количество ошибок для вывода

    def add_error(self, error: CompilationError):
        """Добавить ошибку в список"""
        self.errors.append(error)

        # Если достигли максимального количества ошибок, вывести сообщение и прекратить
        if len(self.errors) >= self.max_errors:
            print(
                f"Too many errors ({self.max_errors}), stopping compilation.",
                file=sys.stderr,
            )
            self.report_errors()
            sys.exit(1)

    def has_errors(self) -> bool:
        """Проверить, есть ли ошибки"""
        return len(self.errors) > 0

    def error_count(self) -> int:
        """Получить количество ошибок"""
        return len(self.errors)

    def report_errors(self):
        """Вывести все накопленные ошибки"""
        for error in self.errors:
            print(error, file=sys.stderr)

    def reset(self):
        """Сбросить все ошибки"""
        self.errors.clear()


def report_error(error: CompilationError) -> None:
    print(error, file=sys.stderr)
