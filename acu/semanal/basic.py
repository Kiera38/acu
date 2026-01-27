from __future__ import annotations
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Any, Generator

from acu.parser import ExprVisitor, Location, StmtVisitor, nodes
from acu.semanal import ir, types

builtin_types = {
    "Nothing": types.nothing_type,
    "Bool": types.bool_type,
    "Int": types.int_type,
    "Int8": types.int8_type,
    "Int16": types.int16_type,
    "Int32": types.int32_type,
    "Int64": types.int64_type,
    "UInt": types.uint_type,
    "UInt8": types.uint8_type,
    "UInt16": types.uint16_type,
    "UInt32": types.uint32_type,
    "UInt64": types.uint64_type,
    "Float": types.float_type,
    "Float32": types.float32_type,
    "Float64": types.float64_type,
}


def get_int_constant(expr: nodes.Expr) -> int:
    if not isinstance(expr, nodes.LiteralExpr):
        raise Exception("must be a literal")
    if not isinstance(expr.value, int):
        raise Exception("not a int")
    return expr.value


@dataclass
class Scope:
    vars: dict[str, ir.VarDecl | ir.Arg]
    funcs: dict[str, ir.Func]
    structs: dict[str, types.Struct]
    is_loop: bool
    is_function: bool


class Context:
    def __init__(self) -> None:
        self.scopes: list[Scope] = []
        self.push_scope()
        self.blocks: list[ir.Block] = []

    def push_scope(self, is_loop: bool = False, is_function: bool = False) -> None:
        self.scopes.append(Scope({}, {}, {}, is_loop, is_function))

    def pop_scope(self) -> None:
        self.scopes.pop()

    @contextmanager
    def block(self, block: ir.Block):
        self.blocks.append(block)
        yield
        self.blocks.pop()

    def add(self, inst: ir.Inst):
        self.blocks[-1].code.append(inst)
        return inst

    def find(self, name: str) -> ir.VarDecl | ir.Arg | ir.Func | types.Struct:
        for scope in reversed(self.scopes):
            if var := scope.vars.get(name):
                return var
            if func := scope.funcs.get(name):
                return func
            if struct := scope.structs.get(name):
                return struct
        raise Exception(f"name not found {name}")

    def add_var(self, var: ir.VarDecl | ir.Arg) -> None:
        self.scopes[-1].vars[var.name] = var

    def add_struct(self, name: str, location: Location) -> types.Struct:
        s = self.scopes[-1].structs[name] = types.Struct(name, {}, location)
        return s

    def get_struct(self, name: str) -> types.Struct:
        return self.scopes[-1].structs[name]

    def add_func(self, name: str) -> ir.Func:
        f = self.scopes[-1].funcs[name] = ir.Func(name, 0, types.Type(), ir.Block([]))
        return f

    def get_func(self, name: str) -> ir.Func:
        return self.scopes[-1].funcs[name]

    @property
    def in_function(self) -> bool:
        for scope in reversed(self.scopes):
            if scope.is_function:
                return True
        return False

    @property
    def in_loop(self) -> bool:
        for scope in reversed(self.scopes):
            if scope.is_loop:
                return True
        return False


class TypeConverter(ExprVisitor[types.Type]):
    def __init__(self, context: Context) -> None:
        super().__init__()
        self.context = context

    def name(self, expr: nodes.NameExpr) -> types.Type:
        if type := builtin_types.get(expr.name):
            return type
        if struct := self.context.find(expr.name):
            if not isinstance(struct, ir.Struct):
                raise Exception("is not struct")
            return types.StructType(struct)
        raise Exception("unknown type")

    def get_item(self, expr: nodes.GetItemExpr) -> ir.Type:
        if not isinstance(expr.value, nodes.NameExpr):
            raise Exception("unknown type")

        if expr.value.name == "Array":
            if len(expr.args) != 2:
                raise Exception("unknown type")
            type = expr.args[0].accept(self)
            length = get_int_constant(expr.args[1])
            return types.ArrayType(type, length)

        if expr.value.name == "Ptr":
            if len(expr.args) != 1:
                raise Exception("unknown type")
            return types.PointerType(expr.args[0].accept(self))

        raise Exception("unknown type")


logical_op = {
    nodes.BinaryOp.LOGICAL_AND: ir.LogicalOp.AND,
    nodes.BinaryOp.LOGICAL_OR: ir.LogicalOp.OR,
}

binary_op = {
    nodes.BinaryOp.ADD: ir.BinaryOp.ADD,
    nodes.BinaryOp.SUB: ir.BinaryOp.SUB,
    nodes.BinaryOp.MUL: ir.BinaryOp.MUL,
    nodes.BinaryOp.DIV: ir.BinaryOp.DIV,
    nodes.BinaryOp.MOD: ir.BinaryOp.MOD,
    nodes.BinaryOp.LSHIFT: ir.BinaryOp.LSHIFT,
    nodes.BinaryOp.RSHIFT: ir.BinaryOp.RSHIFT,
    nodes.BinaryOp.BIT_AND: ir.BinaryOp.BIT_AND,
    nodes.BinaryOp.BIT_OR: ir.BinaryOp.BIT_OR,
    nodes.BinaryOp.BIT_XOR: ir.BinaryOp.BIT_XOR,
}

unary_op = {
    nodes.UnaryOp.BIT_NOT: ir.UnaryOp.BIT_NOT,
    nodes.UnaryOp.NEG: ir.UnaryOp.NEG,
    nodes.UnaryOp.NOT: ir.UnaryOp.NOT,
}

comparison_op = {
    nodes.ComparisonOp.EQUAL: ir.ComparisonOp.EQUAL,
    nodes.ComparisonOp.GREATER: ir.ComparisonOp.GREATER,
    nodes.ComparisonOp.GREATER_EQUAL: ir.ComparisonOp.GREATER_EQUAL,
    nodes.ComparisonOp.LESS: ir.ComparisonOp.LESS,
    nodes.ComparisonOp.LESS_EQUAL: ir.ComparisonOp.LESS_EQUAL,
    nodes.ComparisonOp.NOT_EQUAL: ir.ComparisonOp.NOT_EQUAL,
}


class ExprConverter(ExprVisitor[ir.Inst]):
    def __init__(self, typeconv: TypeConverter, context: Context) -> None:
        super().__init__()
        self.types = typeconv
        self.context = context

    def accept(self, expr: nodes.Expr) -> ir.Inst:
        return self.context.add(expr.accept(self))

    def block(self, expr: nodes.Expr) -> ir.Block:
        block = ir.Block([])
        with self.context.block(block):
            self.accept(expr)
        return block

    def name(self, expr: nodes.NameExpr) -> ir.Inst:
        var = self.context.find(expr.name)
        if isinstance(var, (ir.Func, types.Struct)):
            return ir.Literal(expr.location, var)
        return ir.Load(expr.location, var)

    def literal(self, expr: nodes.LiteralExpr) -> ir.Inst:
        return ir.Literal(expr.location, expr.value)

    def binary(self, expr: nodes.BinaryExpr) -> ir.Inst:
        if expr.op in (nodes.BinaryOp.LOGICAL_OR, nodes.BinaryOp.LOGICAL_AND):
            return ir.Logical(
                expr.location,
                self.accept(expr.left),
                self.block(expr.right),
                logical_op[expr.op],
            )
        return ir.Binary(
            expr.location,
            self.accept(expr.left),
            self.accept(expr.right),
            binary_op[expr.op],
        )

    def unary(self, expr: nodes.UnaryExpr) -> ir.Inst:
        if expr.op == nodes.UnaryOp.ADDRESS_OF:
            return ir.AddressOf(expr.location, self.accept(expr.operand))
        if expr.op == nodes.UnaryOp.DEREF:
            return ir.Deref(expr.location, self.accept(expr.operand))
        return ir.Unary(expr.location, self.accept(expr.operand), unary_op[expr.op])

    def comparison(self, expr: nodes.ComparisonExpr) -> ir.Inst:
        return ir.Comparison(
            expr.location,
            self.accept(expr.operands[0]),
            [
                ir.Comparator(self.block(val), comparison_op[op])
                for val, op in zip(expr.operands[1:], expr.operators)
            ],
        )

    def call(self, expr: nodes.CallExpr) -> ir.Inst:
        return ir.Call(
            expr.location,
            self.accept(expr.value),
            [self.accept(arg) for arg in expr.args],
        )

    def get_item(self, expr: nodes.GetItemExpr) -> ir.Inst:
        if len(expr.args) != 1:
            raise Exception("unsupported get iitem")
        return ir.GetItem(
            expr.location, self.accept(expr.value), self.accept(expr.args[0])
        )

    def get_attr(self, expr: nodes.GetAttrExpr) -> ir.Inst:
        return ir.GetAttr(expr.location, self.accept(expr.value), expr.name)

    def array(self, expr: nodes.ArrayExpr) -> ir.Inst:
        return ir.Array(expr.location, [self.accept(item) for item in expr.items])
    
    def as_expr(self, expr: nodes.AsExpr) -> ir.Inst:
        return ir.AsInst(
            expr.location,
            self.accept(expr.value),
            expr.type.accept(self.types),
        )


class StoreConverter(ExprVisitor[ir.Inst]):
    def __init__(
        self, context: Context, exprs: ExprConverter, value: ir.Inst, location: Location
    ) -> None:
        super().__init__()
        self.context = context
        self.exprs = exprs
        self.value = value
        self.location = location

    def expr(self, expr: nodes.Expr) -> ir.Inst:
        raise Exception("no use this in left of assign")

    def accept(self, expr: nodes.Expr) -> ir.Inst:
        return self.context.add(expr.accept(self))

    def name(self, expr: nodes.NameExpr) -> ir.Inst:
        var = self.context.find(expr.name)
        if isinstance(var, (ir.Func, types.Struct)):
            raise Exception("func or struct in left of assign")
        return ir.Store(self.location, var, self.value)

    def get_item(self, expr: nodes.GetItemExpr) -> ir.Inst:
        return ir.SetItem(
            expr.location,
            self.exprs.accept(expr.value),
            self.exprs.accept(expr.args[0]),
            self.value,
        )

    def get_attr(self, expr: nodes.GetAttrExpr) -> ir.Inst:
        return ir.SetAttr(
            expr.location, self.exprs.accept(expr.value), self.value, expr.name
        )


op_assign = {
    nodes.AssignOp.ADD: ir.BinaryOp.ADD,
    nodes.AssignOp.SUB: ir.BinaryOp.SUB,
    nodes.AssignOp.MUL: ir.BinaryOp.MUL,
    nodes.AssignOp.DIV: ir.BinaryOp.DIV,
    nodes.AssignOp.MOD: ir.BinaryOp.MOD,
    nodes.AssignOp.LSHIFT: ir.BinaryOp.LSHIFT,
    nodes.AssignOp.RSHIFT: ir.BinaryOp.RSHIFT,
    nodes.AssignOp.BIT_AND: ir.BinaryOp.BIT_AND,
    nodes.AssignOp.BIT_OR: ir.BinaryOp.BIT_OR,
    nodes.AssignOp.BIT_XOR: ir.BinaryOp.BIT_XOR,
}


class StmtConverter(StmtVisitor[None]):
    def __init__(
        self, typeconv: TypeConverter, exprconv: ExprConverter, context: Context
    ) -> None:
        super().__init__()
        self.types = typeconv
        self.exprs = exprconv
        self.context = context

    def add[T: ir.Inst](self, inst: T) -> T:
        self.context.add(inst)
        return inst

    def expr_stmt(self, stmt: nodes.ExprStmt):
        self.exprs.accept(stmt.expr)

    def var(self, stmt: nodes.VarStmt):
        var = self.add(
            ir.VarDecl(
                stmt.location,
                stmt.name,
                stmt.type.accept(self.types) if stmt.type else None,
            )
        )
        self.context.add_var(var)
        # If there's an initializer, emit a Store to initialize the variable.
        if stmt.init is not None:
            val = self.exprs.accept(stmt.init)
            self.add(ir.Store(stmt.location, var, val))

    def block(self, stmt: nodes.BlockStmt):
        for s in stmt.stmts:
            s.accept(self)

    @contextmanager
    def scope(self, is_loop: bool = False) -> Generator[None, Any, None]:
        self.context.push_scope(is_loop)
        yield
        self.context.pop_scope()

    def get_block(self, stmt: nodes.Stmt) -> ir.Block:
        block = ir.Block([])
        with self.context.block(block):
            stmt.accept(self)
        return block

    def if_stmt(self, stmt: nodes.IfStmt):
        expr = self.exprs.accept(stmt.cond)
        with self.scope():
            then = self.get_block(stmt.then_block)
        if stmt.else_block:
            with self.scope():
                else_block = self.get_block(stmt.else_block)
        else:
            else_block = ir.Block([])
        self.add(ir.If(stmt.location, expr, then, else_block))

    def while_stmt(self, stmt: nodes.WhileStmt):
        expr = self.exprs.accept(stmt.cond)
        with self.scope(is_loop=True):
            then = self.get_block(stmt.body)
        else_block = ir.Block([ir.Break(stmt.location)])
        self.add(
            ir.Loop(
                stmt.location,
                ir.Block([ir.If(stmt.location, expr, then, else_block)]),
            )
        )

    def return_stmt(self, stmt: nodes.ReturnStmt):
        if not self.context.in_function:
            raise Exception("return not in function")
        self.add(
            ir.Return(
                stmt.location,
                self.exprs.accept(stmt.value) if stmt.value is not None else None,
            )
        )

    def break_stmt(self, stmt: nodes.BreakStmt):
        if not self.context.in_loop:
            raise Exception("break not in loop")
        self.add(ir.Break(stmt.location))

    def continue_stmt(self, stmt: nodes.ContinueStmt):
        if not self.context.in_loop:
            raise Exception("continue not in loop")
        self.add(ir.Continue(stmt.location))

    def assign(self, stmt: nodes.AssignStmt):
        expr = self.exprs.accept(stmt.value)
        converter = StoreConverter(self.context, self.exprs, expr, stmt.location)
        for target in stmt.targets:
            converter.accept(target)

    def op_assign(self, stmt: nodes.OpAssignStmt):
        target = self.exprs.accept(stmt.target)
        value = self.exprs.accept(stmt.value)
        StoreConverter(
            self.context,
            self.exprs,
            self.add(ir.Binary(stmt.location, target, value, op_assign[stmt.op])),
            stmt.location,
        ).accept(stmt.target)


def convert_struct(
    struct: nodes.Struct, typeconv: TypeConverter, context: Context
) -> ir.Struct:
    ir_struct = context.get_struct(struct.name)
    ir_struct.fields = {
        field.name: types.StructField(field.type.accept(typeconv), i, field.location)
        for i, field in enumerate(struct.fields)
    }
    return ir_struct


def convert_func(
    func: nodes.Func, typeconv: TypeConverter, stmts: StmtConverter, context: Context
) -> ir.Func:
    ir_func = context.get_func(func.name)
    ir_func.arg_count = len(func.args)
    context.push_scope(is_function=True)
    if func.return_type:
        ir_func.return_type = func.return_type.accept(typeconv)
    else:
        ir_func.return_type = types.nothing_type
    code = []
    for arg in func.args:
        ir_arg = ir.Arg(
            arg.location, arg.name, arg.type.accept(typeconv) if arg.type else None
        )
        code.append(ir_arg)
        context.add_var(ir_arg)
    ir_func.code = ir.Block(code)
    with context.block(ir_func.code):
        func.body.accept(stmts)
        if not ir_func.code.code or not isinstance(ir_func.code.code[-1], ir.Return):
            if ir_func.return_type == types.nothing_type:
                stmts.add(ir.Return(func.location, None))
            else:
                raise Exception("missing return statement")
    context.pop_scope()
    return ir_func


def convert_module(module: nodes.Module) -> ir.Module:
    context = Context()
    for struct in module.structs:
        context.add_struct(struct.name, struct.location)
    for func in module.funcs:
        context.add_func(func.name)

    types = TypeConverter(context)
    exprs = ExprConverter(types, context)
    stmts = StmtConverter(types, exprs, context)
    structs = [convert_struct(struct, types, context) for struct in module.structs]
    funcs = [convert_func(func, types, stmts, context) for func in module.funcs]
    return ir.Module(funcs, structs)
