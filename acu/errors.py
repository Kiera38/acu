import sys
from dataclasses import dataclass

from acu.source import Source, Location


@dataclass
class CompilationError(Exception):
    location: Location
    message: str
    source: Source | None = None

    def __str__(self) -> str:
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


def report_error(error: CompilationError) -> None:
    print(error, file=sys.stderr)
