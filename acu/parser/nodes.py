from __future__ import annotations
from dataclasses import dataclass
from enum import Enum, auto
from acu.parser.lexer import Location
from mypy_extensions import trait


@trait
class ExprVisitor[T]:
    def expr(self, expr: Expr) -> T: # type: ignore
        pass

    def literal(self, expr: LiteralExpr) -> T:
        return self.expr(expr)

    def name(self, expr: NameExpr) -> T:
        return self.expr(expr)

    def binary(self, expr: BinaryExpr) -> T:
        return self.expr(expr)

    def comparison(self, expr: ComparisonExpr) -> T:
        return self.expr(expr)

    def unary(self, expr: UnaryExpr) -> T:
        return self.expr(expr)

    def call(self, expr: CallExpr) -> T:
        return self.expr(expr)

    def get_item(self, expr: GetItemExpr) -> T:
        return self.expr(expr)

    def get_attr(self, expr: GetAttrExpr) -> T:
        return self.expr(expr)

    def array(self, expr: ArrayExpr) -> T:
        return self.expr(expr)
    
    def as_expr(self, expr: AsExpr) -> T:
        return self.expr(expr)


@trait
class StmtVisitor[T]:
    def stmt(self, stmt: Stmt) -> T: # type: ignore
        pass

    def expr_stmt(self, stmt: ExprStmt) -> T:
        return self.stmt(stmt)

    def var(self, stmt: VarStmt) -> T:
        return self.stmt(stmt)

    def block(self, stmt: BlockStmt) -> T:
        return self.stmt(stmt)

    def if_stmt(self, stmt: IfStmt) -> T:
        return self.stmt(stmt)

    def while_stmt(self, stmt: WhileStmt) -> T:
        return self.stmt(stmt)

    def return_stmt(self, stmt: ReturnStmt) -> T:
        return self.stmt(stmt)

    def break_stmt(self, stmt: BreakStmt) -> T:
        return self.stmt(stmt)

    def continue_stmt(self, stmt: ContinueStmt) -> T:
        return self.stmt(stmt)

    def assign(self, stmt: AssignStmt) -> T:
        return self.stmt(stmt)

    def op_assign(self, stmt: OpAssignStmt) -> T:
        return self.stmt(stmt)


@trait
class NodeVisitor[T](ExprVisitor[T], StmtVisitor[T]):
    def node(self, node: Node) -> T: # type: ignore
        pass

    def expr(self, expr: Expr) -> T:
        return self.node(expr)

    def stmt(self, stmt: Stmt) -> T:
        return self.node(stmt)

    def func_arg(self, node: FuncArg) -> T:
        return self.node(node)

    def func(self, node: Func) -> T:
        return self.node(node)

    def struct_field(self, node: StructField) -> T:
        return self.node(node)

    def struct(self, node: Struct) -> T:
        return self.node(node)


@dataclass
class Node:
    location: Location

    def accept[T](self, visitor: NodeVisitor[T]) -> T:
        return visitor.node(self)


@dataclass
class Expr(Node):
    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.expr(self)


@dataclass
class LiteralExpr(Expr):
    value: int | float | str
    is_char = False

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.literal(self)


@dataclass
class NameExpr(Expr):
    name: str

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.name(self)


class BinaryOp(Enum):
    ADD = auto()
    SUB = auto()
    MUL = auto()
    DIV = auto()
    MOD = auto()

    LSHIFT = auto()
    RSHIFT = auto()
    BIT_AND = auto()
    BIT_OR = auto()
    BIT_XOR = auto()

    LOGICAL_AND = auto()
    LOGICAL_OR = auto()


@dataclass
class BinaryExpr(Expr):
    left: Expr
    right: Expr
    op: BinaryOp

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.binary(self)


class ComparisonOp(Enum):
    LESS = auto()
    GREATER = auto()
    LESS_EQUAL = auto()
    GREATER_EQUAL = auto()
    EQUAL = auto()
    NOT_EQUAL = auto()


@dataclass
class ComparisonExpr(Expr):
    operands: list[Expr]
    operators: list[ComparisonOp]

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.comparison(self)


class UnaryOp(Enum):
    NOT = auto()
    NEG = auto()
    BIT_NOT = auto()
    DEREF = auto()
    ADDRESS_OF = auto()


@dataclass
class UnaryExpr(Expr):
    operand: Expr
    op: UnaryOp

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.unary(self)


@dataclass
class CallExpr(Expr):
    value: Expr
    args: list[Expr]

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.call(self)


@dataclass
class GetItemExpr(Expr):
    value: Expr
    args: list[Expr]

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.get_item(self)


@dataclass
class GetAttrExpr(Expr):
    value: Expr
    name: str

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.get_attr(self)


@dataclass
class ArrayExpr(Expr):
    items: list[Expr]

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.array(self)
    

@dataclass
class AsExpr(Expr):
    value: Expr
    type: Expr

    def accept[T](self, visitor: ExprVisitor[T]) -> T:
        return visitor.as_expr(self)


@dataclass
class Stmt(Node):
    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.stmt(self)


@dataclass
class ExprStmt(Stmt):
    expr: Expr

    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.expr_stmt(self)


@dataclass
class VarStmt(Stmt):
    name: str
    type: Expr | None
    init: Expr | None

    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.var(self)


@dataclass
class BlockStmt(Stmt):
    stmts: list[Stmt]

    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.block(self)


@dataclass
class IfStmt(Stmt):
    cond: Expr
    then_block: Stmt
    else_block: Stmt | None

    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.if_stmt(self)


@dataclass
class WhileStmt(Stmt):
    cond: Expr
    body: Stmt

    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.while_stmt(self)


@dataclass
class ReturnStmt(Stmt):
    value: Expr | None

    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.return_stmt(self)


@dataclass
class BreakStmt(Stmt):
    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.break_stmt(self)


@dataclass
class ContinueStmt(Stmt):
    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.continue_stmt(self)


@dataclass
class AssignStmt(Stmt):
    targets: list[Expr]
    value: Expr

    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.assign(self)


class AssignOp(Enum):
    ADD = auto()
    SUB = auto()
    MUL = auto()
    DIV = auto()
    MOD = auto()

    LSHIFT = auto()
    RSHIFT = auto()
    BIT_AND = auto()
    BIT_OR = auto()
    BIT_XOR = auto()


@dataclass
class OpAssignStmt(Stmt):
    target: Expr
    value: Expr
    op: AssignOp

    def accept[T](self, visitor: StmtVisitor[T]) -> T:
        return visitor.op_assign(self)


@dataclass
class FuncArg(Node):
    name: str
    type: Expr

    def accept[T](self, visitor: NodeVisitor[T]) -> T:
        return visitor.func_arg(self)


@dataclass
class Func(Node):
    name: str
    args: list[FuncArg]
    return_type: Expr | None
    body: Stmt

    def accept[T](self, visitor: NodeVisitor[T]) -> T:
        return visitor.func(self)


@dataclass
class StructField(Node):
    name: str
    type: Expr

    def accept[T](self, visitor: NodeVisitor[T]) -> T:
        return visitor.struct_field(self)


@dataclass
class Struct(Node):
    name: str
    fields: list[StructField]

    def accept[T](self, visitor: NodeVisitor[T]) -> T:
        return visitor.struct(self)


@dataclass
class Module:
    funcs: list[Func]
    structs: list[Struct]
