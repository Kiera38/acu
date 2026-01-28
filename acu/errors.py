import sys
from dataclasses import dataclass, field
from typing import List

from termcolor import colored

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
        output = []

        # Получить строки с контекстом (1 строка до, 1 после)
        start_line = max(1, self.location.line - 1)
        end_line = min(len(self.source.lines), self.location.end_line + 1)
        
        # Найти максимальную длину номера строки для выравнивания
        max_line_num = end_line
        line_num_width = len(str(max_line_num))
        
        # Заголовок с ошибкой
        error_label = colored("Error", "red", attrs=["bold"])
        output.append(f"{error_label}: {self.message}")
        
        # Информация о локации
        location_info = colored(
            f"{self.source.name}:{self.location.line}:{self.location.column}",
            "cyan"
        )
        output.append(f"  at {location_info}")
        output.append(colored(" " * line_num_width + " |", "dark_grey"))
        
        # Вывести строки контекста
        for line_num in range(start_line, end_line + 1):
            line_content = self.source.lines[line_num - 1]
            line_prefix = f"{line_num:>{line_num_width}} | "
            
            # Если это строка с ошибкой, выделить её
            if self.location.line <= line_num <= self.location.end_line:
                output.append(colored(line_prefix, "dark_grey") + line_content)
                
                # Вывести указатель на ошибку
                if line_num == self.location.line and line_num == self.location.end_line:
                    # Однострочная ошибка
                    pointer_start = self.location.column - 1
                    pointer_length = self.location.end_column - self.location.column + 1
                    pointer = " " * pointer_start + "^" * pointer_length
                    output.append(colored(" " * line_num_width + " | ", "dark_grey") + colored(pointer, "red", attrs=["bold"]))
                elif line_num == self.location.line:
                    # Первая строка многострочной ошибки
                    pointer_start = self.location.column - 1
                    pointer_length = len(line_content) - pointer_start
                    pointer = " " * pointer_start + "^" * pointer_length
                    output.append(colored(" " * line_num_width + " | ", "dark_grey") + colored(pointer, "red", attrs=["bold"]))
                elif line_num == self.location.end_line:
                    # Последняя строка многострочной ошибки
                    pointer = "^" * self.location.end_column
                    output.append(colored(" " * line_num_width + " | ", "dark_grey") + colored(pointer, "red", attrs=["bold"]))
                else:
                    # Средние строки многострочной ошибки
                    pointer = "^" * len(line_content)
                    output.append(colored(" " * line_num_width + " | ", "dark_grey") + colored(pointer, "red", attrs=["bold"]))
            else:
                # Строки контекста (не с ошибкой)
                output.append(colored(line_prefix, "dark_grey") + line_content)

        # Вывести примечания
        for note in self.notes:
            note_label = colored("note", "yellow", attrs=["bold"])
            output.append(colored(" " * line_num_width + " | ", "dark_grey") + f"{note_label}: {note.message}")

        # Вывести подсказки
        for help_item in self.helps:
            help_label = colored("help", "cyan", attrs=["bold"])
            output.append(colored(" " * line_num_width + " = ", "dark_grey") + f"{help_label}: {help_item.message}")
        
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
        for i, error in enumerate(self.errors, 1):
            print(error.format_message(), file=sys.stderr)
            if i < len(self.errors):
                print(file=sys.stderr)  # Пустая строка между ошибками
        
        # Вывести сводку
        if self.error_count() > 0:
            error_count = self.error_count()
            summary = colored(
                f"error: aborting due to {error_count} compilation error{'s' if error_count != 1 else ''}",
                "red",
                attrs=["bold"]
            )
            print(file=sys.stderr)
            print(summary, file=sys.stderr)

    def reset(self):
        """Сбросить все ошибки"""
        self.errors.clear()


def report_error(error: CompilationError) -> None:
    print(error, file=sys.stderr)
