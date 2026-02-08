from __future__ import annotations

from contextlib import contextmanager
from typing import Any, Generator

from acu.errors import CompilationError, Note
from acu.parser import ExprVisitor, Location, StmtVisitor, nodes
from acu.semanal import ir, types
from acu.semanal.context import Context, UsedPackage
from acu.source import Source

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


def get_int_constant(expr: nodes.Expr, source: Source) -> int:
    if not isinstance(expr, nodes.LiteralExpr):
        raise CompilationError(
            expr.location,
            "must be a literal",
            source,
            helps=[Note("Use a numeric literal like 10, 42, etc.")],
        )
    if not isinstance(expr.value, int):
        raise CompilationError(
            expr.location,
            "not a int",
            source,
            helps=[Note("Expected an integer literal, got a different type")],
        )
    return expr.value


class TypeConverter(ExprVisitor[types.Type]):
    def __init__(self, context: Context) -> None:
        super().__init__()
        self.context = context

    def name(self, expr: nodes.NameExpr) -> types.Type:
        if type := builtin_types.get(expr.name):
            return type
        if struct := self.context.find(expr.name, expr.location):
            if not isinstance(struct, ir.Struct):
                raise CompilationError(
                    expr.location, "is not struct", self.context.source
                )
            return types.StructType(struct)
        raise CompilationError(expr.location, "unknown type", self.context.source)

    def get_item(self, expr: nodes.GetItemExpr) -> ir.Type:
        if not isinstance(expr.value, nodes.NameExpr):
            raise CompilationError(expr.location, "unknown type", self.context.source)

        if expr.value.name == "Array":
            if len(expr.args) != 2:
                raise CompilationError(
                    expr.location, "unknown type", self.context.source
                )
            type = expr.args[0].accept(self)
            length = get_int_constant(expr.args[1], self.context.source)
            return types.ArrayType(type, length)

        if expr.value.name == "Ptr":
            if len(expr.args) != 1:
                raise CompilationError(
                    expr.location, "unknown type", self.context.source
                )
            return types.PointerType(expr.args[0].accept(self))

        raise CompilationError(expr.location, "unknown type", self.context.source)


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


class GetAttrConverter(ExprVisitor[ir.Inst | Context | UsedPackage]):
    def __init__(self, context: Context) -> None:
        super().__init__()
        self.context = context

    def name(self, expr: nodes.NameExpr) -> ir.Inst | Context | UsedPackage:
        var = self.context.find(expr.name, expr.location)
        if isinstance(var, (ir.Func, types.Struct)):
            return ir.Literal(expr.location, var)
        if isinstance(var, (Context, UsedPackage)):
            return var
        return ir.Load(expr.location, var)
    
    def get_attr(self, expr: nodes.GetAttrExpr) -> ir.Inst | Context | UsedPackage:
        value = expr.value.accept(self)
        if isinstance(value, ir.Inst):
            self.context.add(value)
            return ir.GetAttr(expr.location, value, expr.name)
        if isinstance(value, Context):
            result = value.find(expr.name, expr.location)
            assert isinstance(result, (ir.Func, types.Struct))
            return ir.Literal(expr.location, result)
        assert isinstance(value, UsedPackage)
        return value.find(expr.name, expr.location)


class ExprConverter(ExprVisitor[ir.Inst]):
    def __init__(self, typeconv: TypeConverter, get_attr_conv: GetAttrConverter, context: Context) -> None:
        super().__init__()
        self.types = typeconv
        self.get_attr_conv = get_attr_conv
        self.context = context

    def accept(self, expr: nodes.Expr) -> ir.Inst:
        return self.context.add(expr.accept(self))

    def block(self, expr: nodes.Expr) -> ir.Block:
        block = ir.Block([])
        with self.context.block(block):
            self.accept(expr)
        return block

    def name(self, expr: nodes.NameExpr) -> ir.Inst:
        var = expr.accept(self.get_attr_conv)
        assert isinstance(var, ir.Inst)
        return var

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
            raise CompilationError(
                expr.location, "unsupported get item", self.context.source
            )
        return ir.GetItem(
            expr.location, self.accept(expr.value), self.accept(expr.args[0])
        )

    def get_attr(self, expr: nodes.GetAttrExpr) -> ir.Inst:
        var = expr.accept(self.get_attr_conv)
        assert isinstance(var, ir.Inst)
        return var

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
        raise CompilationError(
            expr.location,
            "cannot use this expression on the left side of assignment",
            self.context.source,
        )

    def accept(self, expr: nodes.Expr) -> ir.Inst:
        return self.context.add(expr.accept(self))

    def name(self, expr: nodes.NameExpr) -> ir.Inst:
        var = self.context.find(expr.name, expr.location)
        if isinstance(var, (ir.Func, types.Struct, Context)):
            raise CompilationError(
                expr.location,
                "function, struct or module cannot be assigned to",
                self.context.source,
            )
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
            raise CompilationError(
                stmt.location,
                "'return' statement not inside function",
                self.context.source,
            )
        self.add(
            ir.Return(
                stmt.location,
                self.exprs.accept(stmt.value) if stmt.value is not None else None,
            )
        )

    def break_stmt(self, stmt: nodes.BreakStmt):
        if not self.context.in_loop:
            raise CompilationError(
                stmt.location, "'break' statement not inside loop", self.context.source
            )
        self.add(ir.Break(stmt.location))

    def continue_stmt(self, stmt: nodes.ContinueStmt):
        if not self.context.in_loop:
            raise CompilationError(
                stmt.location,
                "'continue' statement not inside loop",
                self.context.source,
            )
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
) -> None:
    ir_struct = context.get_struct(struct.name)
    ir_struct.fields = {
        field.name: types.StructField(field.type.accept(typeconv), i, field.location)
        for i, field in enumerate(struct.fields)
    }


def convert_func(
    func: nodes.Func, typeconv: TypeConverter, stmts: StmtConverter, context: Context
) -> None:
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
                raise CompilationError(
                    func.location, "missing return statement", context.source
                )
    context.pop_scope()


def create_module(module: nodes.Module) -> ir.Module:
    funcs = []
    structs = []
    for func in module.funcs:
        funcs.append(ir.Func(func.name, 0, types.Type(), ir.Block([]), func.location))
    for struct in module.structs:
        structs.append(types.Struct(struct.name, {}, struct.location))
    return ir.Module(funcs, structs)


def add_imports(
    module: nodes.Module, context: Context, modules: dict[str, Context]
) -> None:
    for import_stmt in module.imports:
        if isinstance(import_stmt, nodes.UseStmt):
            context.add_qualified_import(
                import_stmt.module_name, modules[".".join(import_stmt.module_name)]
            )
        elif isinstance(import_stmt, nodes.FromUseStmt):
            for item in import_stmt.items:
                local_name = item.alias if item.alias else item.name
                item = modules[".".join(import_stmt.module_name)].find(
                    item.name, item.location
                )
                assert not isinstance(
                    item, (ir.VarDecl, ir.Arg, Context)
                ), "Импортировать можно только функции и структуры"
                context.add_unqualified_import(local_name, item)


def convert(module: nodes.Module, context: Context) -> None:
    types_conv = TypeConverter(context)
    get_attr_conv = GetAttrConverter(context)
    exprs = ExprConverter(types_conv, get_attr_conv, context)
    stmts = StmtConverter(types_conv, exprs, context)
    for struct in module.structs:
        convert_struct(struct, types_conv, context)
    for func in module.funcs:
        convert_func(func, types_conv, stmts, context)
