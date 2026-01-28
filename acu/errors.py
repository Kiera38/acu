import sys
from dataclasses import dataclass, field
from typing import List

from acu.source import Location, Source


@dataclass
class Note:
    message: str
    location: Location | None = None


class CompilationError(Exception):
    """Базовый класс для всех ошибок компиляции"""

    def __init__(
        self,
        location: Location,
        message: str,
        source: Source,
        notes: list[Note] | None = None,
        helps: list[Note] | None = None,
    ):
        self.location = location
        self.message = message
        self.source = source
        self.notes = notes or []
        self.helps = helps or []
        super().__init__(message)

    def format_message(self) -> str:
        output = [f"Error: {self.message}"]
        output.append(f"  at {self.source.name}:{self.location.line}:{self.location.column}")
        output.append("    |")
        output.append(f"{self.location.line:3} | {self.source.lines[self.location.line - 1]}")
        print(self.location)
        pointer = " " * (self.location.column - 1) + "^" * (self.location.end_column - self.location.column + 1)
        output.append(f"    | {pointer}")

        for note in self.notes:
            output.append(f"    | note: {note.message}")
        for help in self.helps:
            output.append(f"    = help: {help.message}")
        return "\n".join(output)


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
            print(error.format_message(), file=sys.stderr)

    def reset(self):
        """Сбросить все ошибки"""
        self.errors.clear()


def report_error(error: CompilationError) -> None:
    print(error, file=sys.stderr)
