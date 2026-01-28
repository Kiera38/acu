from dataclasses import dataclass
from enum import Enum, auto

from acu.errors import CompilationError, Note
from acu.source import Source, Location


class TokenType(Enum):
    FUNC = auto()
    IF = auto()
    ELSE = auto()
    WHILE = auto()
    VAR = auto()
    STRUCT = auto()
    AND = auto()
    OR = auto()
    NOT = auto()
    RETURN = auto()
    BREAK = auto()
    CONTINUE = auto()
    AS = auto()
    TRUE = auto()
    FALSE = auto()

    PLUS = auto()
    MINUS = auto()
    STAR = auto()
    SLASH = auto()
    PERCENT = auto()
    EQUAL = auto()
    PLUS_EQUAL = auto()
    MINUS_EQUAL = auto()
    STAR_EQUAL = auto()
    SLASH_EQUAL = auto()
    PERCENT_EQUAL = auto()

    LESS = auto()
    GREATER = auto()
    LESS_EQUAL = auto()
    GREATER_EQUAL = auto()
    NOT_EQUAL = auto()
    EQUAL_EQUAL = auto()

    PIPE = auto()
    TILDE = auto()
    AMP = auto()
    CARET = auto()
    LESS_LESS = auto()
    GREATER_GREATER = auto()

    PIPE_EQUAL = auto()
    TILDE_EQUAL = auto()
    AMP_EQUAL = auto()
    CARET_EQUAL = auto()
    LESS_LESS_EQUAL = auto()
    GREATER_GREATER_EQUAL = auto()

    LPAREN = auto()
    RPAREN = auto()
    LBRACKET = auto()
    RBRACKET = auto()
    LBRACE = auto()
    RBRACE = auto()

    COLON = auto()
    SEMICOLON = auto()
    COMMA = auto()
    DOT = auto()

    INTEGER = auto()
    FLOAT = auto()
    CHAR = auto()
    STRING = auto()
    IDENTIFIER = auto()

    INDENT = auto()
    DEDENT = auto()
    NEW_LINE = auto()
    END_OF_FILE = auto()


@dataclass
class Token:
    type: TokenType
    location: Location
    value: str | int | float | None = None


keywords = {
    "func": TokenType.FUNC,
    "if": TokenType.IF,
    "else": TokenType.ELSE,
    "while": TokenType.WHILE,
    "var": TokenType.VAR,
    "struct": TokenType.STRUCT,
    "or": TokenType.OR,
    "and": TokenType.AND,
    "not": TokenType.NOT,
    "return": TokenType.RETURN,
    "break": TokenType.BREAK,
    "continue": TokenType.CONTINUE,
    "as": TokenType.AS,
    "true": TokenType.TRUE,
    "false": TokenType.FALSE,
}

escaped_char = {"n": "\n", "t": "\t", "0": "\0", "'": "'", "\\": "\\"}
escaped_string = {"n": "\n", "t": "\t", "0": "\0", '"': '"', "\\": "\\"}

operators = {
    "(": TokenType.LPAREN,
    ")": TokenType.RPAREN,
    "[": TokenType.LBRACKET,
    "]": TokenType.RBRACKET,
    "{": TokenType.LBRACE,
    "}": TokenType.RBRACE,
    ":": TokenType.COLON,
    ";": TokenType.SEMICOLON,
    ",": TokenType.COMMA,
    ".": TokenType.DOT,
    "~": TokenType.TILDE,
    "+": TokenType.PLUS,
    "+=": TokenType.PLUS_EQUAL,
    "-": TokenType.MINUS,
    "-=": TokenType.MINUS_EQUAL,
    "*": TokenType.STAR,
    "*=": TokenType.STAR_EQUAL,
    "/": TokenType.SLASH,
    "/=": TokenType.SLASH_EQUAL,
    "%": TokenType.PERCENT,
    "%=": TokenType.PERCENT_EQUAL,
    "=": TokenType.EQUAL,
    "==": TokenType.EQUAL_EQUAL,
    "!=": TokenType.NOT_EQUAL,
    "<": TokenType.LESS,
    "<=": TokenType.LESS_EQUAL,
    ">": TokenType.GREATER,
    ">=": TokenType.GREATER_EQUAL,
    "|": TokenType.PIPE,
    "|=": TokenType.PIPE_EQUAL,
    "&": TokenType.AMP,
    "&=": TokenType.AMP_EQUAL,
    "^": TokenType.CARET,
    "^=": TokenType.CARET_EQUAL,
}
operator_chars = set("".join(operators))


@dataclass
class Position:
    index: int
    line: int
    column: int


class Lexer:
    def __init__(self, source: Source) -> None:
        self._source = source
        self.source = source.text
        self._pos = Position(0, 1, 1)
        self._begin_of_line = True
        self._dedents = 0
        self._indent_stack = [""]

    def peek(self) -> str:
        if self.at_end():
            return "\0"
        return self.source[self._pos.index]

    def next(self) -> str:
        c = self.peek()
        if c == "\n":
            self._pos = Position(self._pos.index + 1, self._pos.line + 1, 1)
        else:
            self._pos = Position(
                self._pos.index + 1, self._pos.line, self._pos.column + 1
            )
        return c

    def at_end(self) -> bool:
        return self._pos.index >= len(self.source)

    def match(self, c: str) -> bool:
        if self.peek() == c:
            self.next()
            return True
        return False

    def token(
        self,
        type: TokenType,
        start_pos: Position | None = None,
        value: str | int | float | None = None,
    ) -> Token:
        if start_pos is not None:
            return Token(
                type,
                Location(
                    start_pos.line, start_pos.column, self._pos.line, self._pos.column
                ),
                value,
            )
        return Token(
            type,
            Location(
                self._pos.line, self._pos.column, self._pos.line, self._pos.column
            ),
            value,
        )

    def skip_whitespace(self) -> None:
        while not self.at_end() and self.peek() in " \t":
            self.next()

    def skip_comment(self) -> None:
        while not self.at_end() and self.peek() != "\n":
            self.next()

    def check_indent(self) -> Token | None:
        if self._dedents:
            self._dedents -= 1
            if not self._dedents:
                self._begin_of_line = False
            return self.token(TokenType.DEDENT)
        if self.at_end() and len(self._indent_stack) > 1:
            self._indent_stack.pop()
            return self.token(TokenType.DEDENT)

        while not self.at_end():
            start = self._pos
            while self.peek() in " \t":
                self.next()
            if self.match("#"):
                self.skip_comment()
                continue
            if self.peek() in "\n\r":
                self.next()
                continue
            if self.at_end():
                return None
            indent = self.source[start.index : self._pos.index]
            prev_indent = self._indent_stack[-1]
            if indent == prev_indent:
                self._begin_of_line = False
                return None
            if len(indent) > len(prev_indent):
                if not indent.startswith(prev_indent):
                    raise CompilationError(
                        Location(start.line, start.column, self._pos.line, self._pos.column),
                        "incorrect indentation: inconsistent tabs and spaces",
                        self._source,
                        helps=[Note("Make sure to use only tabs or only spaces for indentation")]
                    )
                self._indent_stack.append(indent)
                self._begin_of_line = False
                return self.token(TokenType.INDENT, start)

            while len(indent) < len(prev_indent):
                if not prev_indent.startswith(indent):
                    raise CompilationError(
                        Location(start.line, start.column, self._pos.line, self._pos.column),
                        "incorrect indentation: inconsistent tabs and spaces",
                        self._source,
                        helps=[Note("Make sure to use only tabs or only spaces for indentation")]
                    )
                self._dedents += 1
                self._indent_stack.pop()
                prev_indent = self._indent_stack[-1]
            if len(indent) != len(prev_indent):
                raise CompilationError(
                    Location(start.line, start.column, self._pos.line, self._pos.column),
                    "incorrect indentation size",
                    self._source,
                    helps=[Note("Indentation must be consistent with the surrounding code")]
                )
            if prev_indent != indent:
                raise CompilationError(
                    Location(start.line, start.column, self._pos.line, self._pos.column),
                    "incorrect indentation: inconsistent tabs and spaces",
                    self._source,
                    helps=[Note("Make sure to use only tabs or only spaces for indentation")]
                )
            self._dedents -= 1
            if self._dedents == 0:
                self._begin_of_line = False
            return self.token(TokenType.DEDENT, start)
        return None

    def identifier_or_keyword(self) -> Token:
        start = self._pos
        while self.peek().isalnum() or self.peek() == "_":
            self.next()
        id = self.source[start.index : self._pos.index]
        type = keywords.get(id, TokenType.IDENTIFIER)
        return self.token(type, start, id)

    def number(self) -> Token:
        start = self._pos
        text = []
        is_float = False

        while self.peek().isdigit() or self.peek() in "._":
            if self.peek() == ".":
                text.append(".")
                if is_float:
                    raise CompilationError(
                        Location(start.line, start.column, self._pos.line, self._pos.column),
                        "invalid number: multiple decimal points",
                        self._source,
                        helps=[Note("A floating-point number can have only one decimal point")]
                    )
                is_float = True
            elif self.peek() != "_":
                text.append(self.peek())
            self.next()
            if self.at_end():
                break
        text_str = "".join(text)
        if is_float:
            return self.token(TokenType.FLOAT, start, float(text_str))
        return self.token(TokenType.INTEGER, start, int(text_str))

    def hex_number(self) -> Token:
        start = self._pos
        text = []
        self.next()
        self.next()
        while self.peek().isdigit() or self.peek() in "abcdefABCDEF_":
            if self.peek() != "_":
                text.append(self.peek())
            self.next()
            if self.at_end():
                break
        return self.token(TokenType.INTEGER, start, int("".join(text), 16))

    def oct_number(self) -> Token:
        start = self._pos
        text = []
        self.next()
        self.next()
        while self.peek() in "01234567_":
            if self.peek() != "_":
                text.append(self.peek())
            self.next()
            if self.at_end():
                break
        return self.token(TokenType.INTEGER, start, int("".join(text), 8))

    def bin_number(self) -> Token:
        start = self._pos
        text = []
        self.next()
        self.next()
        while self.peek() in "01_":
            if self.peek() != "_":
                text.append(self.peek())
            self.next()
            if self.at_end():
                break
        return self.token(TokenType.INTEGER, start, int("".join(text), 2))

    def character(self) -> Token:
        start = self._pos
        self.next()
        if self.match("\\"):
            escaped = self.next()
            value = escaped_char[escaped]
        else:
            value = self.next()
        if not self.match("'"):
            raise CompilationError(
                Location(start.line, start.column, self._pos.line, self._pos.column),
                "unterminated character literal",
                self._source,
                helps=[Note("Character literals must be enclosed in single quotes like 'a'")]
            )
        return self.token(TokenType.CHAR, start, value)

    def string(self) -> Token:
        start = self._pos
        value = []
        while self.peek() != '"' and not self.at_end():
            c = self.next()
            if c == "\\":
                escaped = self.next()
                value.append(escaped_string[escaped])
            else:
                value.append(c)
        if self.at_end():
            raise CompilationError(
                Location(start.line, start.column, self._pos.line, self._pos.column),
                "unterminated string literal",
                self._source,
                helps=[Note("String literals must be enclosed in double quotes like \"text\"")]
            )
        self.next()
        return self.token(TokenType.STRING, start, "".join(value))

    def operator(self) -> Token:
        start = self._pos
        while (
            self.peek() in operator_chars
            and self.source[start.index : self._pos.index + 1] in operators
        ):
            self.next()
        type = operators[self.source[start.index : self._pos.index]]
        return self.token(type, start)

    def next_token(self) -> Token:
        if self._begin_of_line:
            if token := self.check_indent():
                return token
        self.skip_whitespace()
        if self.at_end():
            return self.token(TokenType.END_OF_FILE)
        c = self.peek()
        if c.isdigit():
            if c == "0":
                nextc = self.source[self._pos.index + 1]
                match nextc:
                    case "x":
                        return self.hex_number()
                    case "o":
                        return self.oct_number()
                    case "b":
                        return self.bin_number()
            return self.number()

        if c.isalpha():
            return self.identifier_or_keyword()
        if c == "'":
            return self.character()
        if c == '"':
            return self.string()
        if c == "\n":
            start = self._pos
            self._begin_of_line = True
            token = self.token(TokenType.NEW_LINE, start)
            self.next()
            return token
        return self.operator()

    def __next__(self) -> Token:
        token = self.next_token()
        if token.type == TokenType.END_OF_FILE:
            raise StopIteration
        return token

    def __iter__(self) -> "Lexer":
        return self
