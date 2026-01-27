from typing import cast

from acu.parser.nodes import *
from acu.parser.lexer import TokenType, Token, Lexer
from acu.parser.nodes import CallExpr, Module


binary_op = {
    TokenType.PLUS: BinaryOp.ADD,
    TokenType.MINUS: BinaryOp.SUB,
    TokenType.STAR: BinaryOp.MUL,
    TokenType.SLASH: BinaryOp.DIV,
    TokenType.PERCENT: BinaryOp.MOD,
    TokenType.LESS_LESS: BinaryOp.LSHIFT,
    TokenType.GREATER_GREATER: BinaryOp.RSHIFT,
    TokenType.PIPE: BinaryOp.BIT_OR,
    TokenType.AMP: BinaryOp.BIT_AND,
    TokenType.CARET: BinaryOp.BIT_XOR,
    TokenType.AND: BinaryOp.LOGICAL_AND,
    TokenType.OR: BinaryOp.LOGICAL_OR,
}

unary_op = {
    TokenType.NOT: UnaryOp.NOT,
    TokenType.MINUS: UnaryOp.NEG,
    TokenType.TILDE: UnaryOp.BIT_NOT,
    TokenType.AMP: UnaryOp.ADDRESS_OF,
    TokenType.STAR: UnaryOp.DEREF
}

comparison_op = {
    TokenType.LESS: ComparisonOp.LESS,
    TokenType.GREATER: ComparisonOp.GREATER,
    TokenType.LESS_EQUAL: ComparisonOp.LESS_EQUAL,
    TokenType.GREATER_EQUAL: ComparisonOp.GREATER_EQUAL,
    TokenType.EQUAL_EQUAL: ComparisonOp.EQUAL,
    TokenType.NOT_EQUAL: ComparisonOp.NOT_EQUAL,
}

assign_op = {
    TokenType.PLUS_EQUAL: AssignOp.ADD,
    TokenType.MINUS_EQUAL: AssignOp.SUB,
    TokenType.STAR_EQUAL: AssignOp.MUL,
    TokenType.SLASH_EQUAL: AssignOp.DIV,
    TokenType.PERCENT_EQUAL: AssignOp.MOD,
    TokenType.LESS_LESS_EQUAL: AssignOp.LSHIFT,
    TokenType.GREATER_GREATER_EQUAL: AssignOp.RSHIFT,
    TokenType.AMP_EQUAL: AssignOp.BIT_AND,
    TokenType.PIPE_EQUAL: AssignOp.BIT_OR,
    TokenType.CARET_EQUAL: AssignOp.BIT_XOR,
}


class Parser:
    def __init__(self, lexer: Lexer) -> None:
        self._lexer = lexer
        self._tokens: list[Token] = []
        self._current = 0

    def parse(self) -> Module:
        funcs = []
        structs = []
        while not self.at_end():
            if self.match(TokenType.FUNC):
                funcs.append(self.parse_function())
            elif self.match(TokenType.STRUCT):
                structs.append(self.parse_struct())
            else:
                raise Exception("Expected function or struct")
        return Module(funcs, structs)

    def peek(self, rel_pos: int = 0) -> Token:
        pos = self._current + rel_pos
        if pos >= len(self._tokens):
            count = pos - len(self._tokens) + 1
            for i in range(count):
                self._tokens.append(self._lexer.next_token())
        return self._tokens[pos]

    def check(self, type: TokenType) -> bool:
        return self.peek().type == type

    def at_end(self) -> bool:
        return self.check(TokenType.END_OF_FILE)

    def next(self) -> Token:
        if not self.at_end():
            self._current += 1
        return self.peek(-1)

    def match(self, type: TokenType) -> bool:
        if self.check(type):
            self.next()
            return True
        return False

    def expect(self, type: TokenType, message: str) -> Token:
        if self.check(type):
            return self.next()
        raise Exception(message)

    def parse_type(self) -> Expr:
        return self.parse_expr()

    def parse_function(self) -> Func:
        name = self.expect(TokenType.IDENTIFIER, "expected function name")
        self.expect(TokenType.LPAREN, "Expected '(' after function name")
        args = []
        while not self.match(TokenType.RPAREN):
            param = self.expect(TokenType.IDENTIFIER, "Expected parameter name")
            self.expect(TokenType.COLON, "Expected ':' after parameter name")
            type = self.parse_type()
            args.append(FuncArg(param.location, cast(str, param.value), type))
            if not self.match(TokenType.COMMA):
                self.expect(TokenType.RPAREN, "Expected ')' after parameters")
                break

        return_type = None
        if not self.match(TokenType.COLON):
            return_type = self.parse_type()
            self.expect(TokenType.COLON, "Expected ':' before functio body")

        return Func(name.location, cast(str, name.value), args, return_type, self.parse_body())

    def parse_struct(self) -> Struct:
        name = self.expect(TokenType.IDENTIFIER, "expected struct name")
        self.expect(TokenType.COLON, "Expected ':' before struct body")
        fields = []
        if self.match(TokenType.NEW_LINE):
            self.expect(TokenType.INDENT, "Expected indented block after new line")
            while not self.match(TokenType.DEDENT):
                fields.append(self.parse_struct_field())
        else:
            fields.append(self.parse_struct_field())
        return Struct(name.location, cast(str, name.value), fields)

    def parse_struct_field(self) -> StructField:
        self.expect(TokenType.VAR, "Field must starts with 'var'")
        name = self.expect(TokenType.IDENTIFIER, "expected field name")
        self.expect(TokenType.COLON, "Expected ':' after field name")
        type = self.parse_type()
        self.expect(TokenType.NEW_LINE, "Expected new line after field")
        return StructField(name.location, cast(str, name.value), type)

    def parse_body(self) -> Stmt:
        result: Stmt
        if self.match(TokenType.NEW_LINE):
            location = self.expect(
                TokenType.INDENT, "Expected indented block after new line"
            ).location
            stmts = []
            while not self.match(TokenType.DEDENT) and not self.at_end():
                stmts.append(self.parse_stmt())
            result = BlockStmt(location, stmts)
        else:
            result = self.parse_stmt()
        return result

    def parse_stmt(self) -> Stmt:
        if self.match(TokenType.VAR):
            return self.parse_var_decl()
        if self.match(TokenType.IF):
            return self.parse_if_stmt()
        if self.match(TokenType.WHILE):
            return self.parse_while_stmt()
        if self.match(TokenType.RETURN):
            return self.parse_return_stmt()
        if self.match(TokenType.BREAK):
            self.expect(TokenType.NEW_LINE, "Expected new line after break")
            return BreakStmt(self.peek(-2).location)
        if self.match(TokenType.CONTINUE):
            self.expect(TokenType.NEW_LINE, "Expected new line after continue")
            return ContinueStmt(self.peek(-2).location)
        return self.parse_assign()

    def parse_var_decl(self) -> VarStmt:
        name = self.expect(TokenType.IDENTIFIER, "expceded variable name")
        type = None
        if self.match(TokenType.COLON):
            type = self.parse_type()
        init = None
        if self.match(TokenType.EQUAL):
            init = self.parse_expr()
        self.expect(TokenType.NEW_LINE, "expectde new line after variable declaration")
        return VarStmt(name.location, cast(str, name.value), type, init)

    def parse_if_stmt(self) -> IfStmt:
        location = self.peek(-1).location
        condition = self.parse_expr()
        self.expect(TokenType.COLON, "Expected ':' after if condition")
        then_block = self.parse_body()
        else_block: Stmt | None = None
        if self.match(TokenType.ELSE):
            if self.match(TokenType.IF):
                else_block = self.parse_if_stmt()
            else:
                self.expect(TokenType.COLON, "Expected ':' after else")
                else_block = self.parse_body()
        return IfStmt(location, condition, then_block, else_block)

    def parse_while_stmt(self) -> WhileStmt:
        location = self.peek(-1).location
        condition = self.parse_expr()
        self.expect(TokenType.COLON, "expected ':' after while condition")
        body = self.parse_body()
        return WhileStmt(location, condition, body)

    def parse_return_stmt(self) -> ReturnStmt:
        location = self.peek(-1).location
        value = None
        if not self.match(TokenType.NEW_LINE):
            value = self.parse_expr()
            self.expect(TokenType.NEW_LINE, "expected new line after")
        return ReturnStmt(location, value)

    def parse_assign(self) -> Stmt:
        expr = self.parse_expr()
        if self.peek().type in assign_op:
            op = self.next()
            value = self.parse_expr()
            self.expect(TokenType.NEW_LINE, "Expected new line")
            return OpAssignStmt(op.location, expr, value, assign_op[op.type])

        targets = [expr]
        location = expr.location
        while self.match(TokenType.EQUAL):
            location = self.peek(-1).location
            targets.append(self.parse_expr())
        self.expect(TokenType.NEW_LINE, "Expected new line")
        expr = targets.pop()
        if not targets:
            return ExprStmt(expr.location, expr)
        return AssignStmt(location, targets, expr)

    def parse_expr(self) -> Expr:
        return self.parse_logical_or()

    def parse_logical_or(self) -> Expr:
        expr = self.parse_logical_and()
        while self.match(TokenType.OR):
            location = self.peek(-1).location
            right = self.parse_logical_and()
            expr = BinaryExpr(location, expr, right, BinaryOp.LOGICAL_OR)
        return expr

    def parse_logical_and(self) -> Expr:
        expr = self.parse_not()
        while self.match(TokenType.AND):
            location = self.peek(-1).location
            right = self.parse_not()
            expr = BinaryExpr(location, expr, right, BinaryOp.LOGICAL_AND)
        return expr

    def parse_not(self) -> Expr:
        if self.match(TokenType.NOT):
            location = self.peek(-1).location
            operand = self.parse_not()
            return UnaryExpr(location, operand, UnaryOp.NOT)
        return self.parse_comparison()

    def parse_comparison(self) -> Expr:
        expr = self.parse_bit_or()
        operands = [expr]
        operators = []
        location = expr.location
        while self.peek().type in comparison_op:
            op = self.next()
            location = op.location
            operators.append(comparison_op[op.type])
            operands.append(self.parse_bit_or())
        if not operators:
            return operands[0]
        return ComparisonExpr(location, operands, operators)

    def parse_bit_or(self) -> Expr:
        expr = self.parse_bit_xor()
        while self.match(TokenType.PIPE):
            location = self.peek(-1).location
            right = self.parse_bit_xor()
            expr = BinaryExpr(location, expr, right, BinaryOp.BIT_OR)
        return expr

    def parse_bit_xor(self) -> Expr:
        expr = self.parse_bit_and()
        while self.match(TokenType.CARET):
            location = self.peek(-1).location
            right = self.parse_bit_and()
            expr = BinaryExpr(location, expr, right, BinaryOp.BIT_XOR)
        return expr

    def parse_bit_and(self) -> Expr:
        expr = self.parse_shift()
        while self.match(TokenType.AMP):
            location = self.peek(-1).location
            right = self.parse_shift()
            expr = BinaryExpr(location, expr, right, BinaryOp.BIT_AND)
        return expr

    def parse_shift(self) -> Expr:
        expr = self.parse_addition()
        while self.peek().type in (TokenType.LESS_LESS, TokenType.GREATER_GREATER):
            op = self.next()
            right = self.parse_addition()
            expr = BinaryExpr(op.location, expr, right, binary_op[op.type])
        return expr

    def parse_addition(self) -> Expr:
        expr = self.parse_multiplication()
        while self.peek().type in (TokenType.PLUS, TokenType.MINUS):
            op = self.next()
            right = self.parse_multiplication()
            expr = BinaryExpr(op.location, expr, right, binary_op[op.type])
        return expr

    def parse_multiplication(self) -> Expr:
        expr = self.parse_unary()
        while self.peek().type in (TokenType.STAR, TokenType.SLASH, TokenType.PERCENT):
            op = self.next()
            right = self.parse_unary()
            expr = BinaryExpr(op.location, expr, right, binary_op[op.type])
        return expr

    def parse_unary(self) -> Expr:
        if self.peek().type in (TokenType.MINUS, TokenType.TILDE):
            op = self.next()
            operand = self.parse_unary()
            return UnaryExpr(op.location, operand, unary_op[op.type])
        return self.parse_postfix()

    def parse_postfix(self) -> Expr:
        expr = self.parse_primary()
        while True:
            match self.peek().type:
                case TokenType.LPAREN:
                    expr = self.parse_call(expr)
                case TokenType.LBRACKET:
                    expr = self.parse_get_item(expr)
                case TokenType.AMP | TokenType.STAR if self.check_next_end():
                    op = self.next()
                    expr = UnaryExpr(op.location, expr, unary_op[op.type])
                case TokenType.DOT:
                    self.next()
                    name = self.expect(
                        TokenType.IDENTIFIER, "expectde attribute name after '.'"
                    )
                    expr = GetAttrExpr(name.location, expr, cast(str, name.value))
                case TokenType.AS:
                    self.next()
                    type = self.parse_type()
                    expr = AsExpr(type.location, expr, type)
                case _:
                    break
        return expr

    def parse_call(self, expr: Expr) -> CallExpr:
        location = self.next().location
        args = []
        while not self.match(TokenType.RPAREN):
            args.append(self.parse_expr())
            if not self.match(TokenType.COMMA):
                self.expect(TokenType.RPAREN, "expected ')' after arguments")
                break
        return CallExpr(location, expr, args)

    def parse_get_item(self, expr: Expr) -> GetItemExpr:
        location = self.next().location
        args = []
        while not self.match(TokenType.RBRACKET):
            args.append(self.parse_expr())
            if not self.match(TokenType.COMMA):
                self.expect(TokenType.RBRACKET, "expected ']' after arguments")
                break
        return GetItemExpr(location, expr, args)

    def check_next_end(self) -> bool:
        # todo: это не нормально
        next_type = self.peek(1).type
        return next_type in (
            TokenType.LPAREN,
            TokenType.RPAREN,
            TokenType.LBRACKET,
            TokenType.RBRACKET,
            TokenType.NEW_LINE,
            TokenType.COLON,
            TokenType.COMMA,
            TokenType.AMP,
            TokenType.AMP_EQUAL,
            TokenType.AND,
            TokenType.CARET,
            TokenType.CARET_EQUAL,
            TokenType.COLON,
            TokenType.DOT,
            TokenType.EQUAL,
            TokenType.EQUAL_EQUAL,
            TokenType.GREATER,
            TokenType.GREATER_EQUAL,
            TokenType.GREATER_GREATER,
            TokenType.GREATER_GREATER_EQUAL,
            TokenType.LESS,
            TokenType.LESS_EQUAL,
            TokenType.LESS_LESS,
            TokenType.LESS_LESS_EQUAL,
            TokenType.LPAREN,
            TokenType.MINUS,
            TokenType.MINUS_EQUAL,
            TokenType.NOT_EQUAL,
            TokenType.OR,
            TokenType.PERCENT,
            TokenType.PERCENT_EQUAL,
            TokenType.PIPE,
            TokenType.PIPE_EQUAL,
            TokenType.PLUS,
            TokenType.PLUS_EQUAL,
            TokenType.SEMICOLON,
            TokenType.SLASH,
            TokenType.SLASH_EQUAL,
            TokenType.STAR,
            TokenType.STAR_EQUAL,
        )

    def parse_primary(self) -> Expr:
        if self.peek().type in (
            TokenType.INTEGER,
            TokenType.FLOAT,
            TokenType.STRING,
            TokenType.CHAR,
        ):
            token = self.next()
            return LiteralExpr(token.location, cast(str | int | float, token.value))
        if self.match(TokenType.TRUE):
            token = self.peek(-1)
            return LiteralExpr(token.location, True)
        if self.match(TokenType.FALSE):
            token = self.peek(-1)
            return LiteralExpr(token.location, False)

        if self.match(TokenType.IDENTIFIER):
            token = self.peek(-1)
            return NameExpr(token.location, cast(str, token.value))

        if self.match(TokenType.LPAREN):
            result = self.parse_expr()
            self.expect(TokenType.RPAREN, "expected ')' after expression in '()")
            return result

        if self.match(TokenType.LBRACKET):
            items = []
            location = self.peek(-1).location
            while not self.match(TokenType.RBRACKET):
                items.append(self.parse_expr())
                if not self.match(TokenType.COMMA):
                    self.expect(TokenType.RBRACKET, "expceted ']' after array items")
                    break
            return ArrayExpr(location, items)

        raise Exception("expected expression")


def parse(source: str) -> Module:
    lexer = Lexer(source)
    parser = Parser(lexer)
    return parser.parse()
