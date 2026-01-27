from __future__ import annotations

from collections.abc import Callable, Iterable

from acu.errors import CompilationError, TypeError, ValidationError
from acu.semanal.ir import (
    AddressOf,
    Arg,
    Array,
    AsInst,
    Binary,
    BinaryOp,
    Block,
    Break,
    Call,
    Comparison,
    Continue,
    Deref,
    Func,
    GetAttr,
    GetItem,
    If,
    Inst,
    InstVisitor,
    Literal,
    Load,
    Logical,
    Loop,
    Return,
    SetAttr,
    SetItem,
    Store,
    Unary,
    UnaryOp,
    VarDecl,
)
from acu.semanal.types import (
    ArrayType,
    FloatType,
    FuncType,
    IntType,
    PointerType,
    Struct,
    StructType,
    Type,
    TypedFunc,
    bool_type,
    float_type,
    int_type,
    nothing_type,
    unify_types,
)
from acu.source import Location, Source


class TypeAnalyzer(InstVisitor[None]):
    def __init__(self, func: Func, source: Source) -> None:
        super().__init__()
        self.func = TypedFunc(func, func.get_type(), source)
        self.refine_map: dict[Inst, Callable[[Type], None]] = {}
        self.first = True
        self._source = source

    def lock_type(self, inst: Inst, type: Type, location: Location):
        self.func.lock_type(inst, type, location)

    def copy_type(self, src_inst: Inst, dst_inst: Inst, location: Location):
        self.func.union(dst_inst, src_inst, location)

    def add_type(self, inst: Inst, type: Type, location: Location):
        old_type = self.func.get_type(inst)
        unified = self.func.add_type(inst, type, location)
        if old_type != unified:
            assert unified is not None
            self.propagate_refined_type(inst, unified)

    def propagate_refined_type(self, inst: Inst, type: Type):
        refine = self.refine_map.get(inst)
        if refine is not None:
            refine(type)

    def propagate(self):
        old_state = self.get_state()
        self.propagate_block(self.func.func.code)
        new_state = self.get_state()
        self.first = False
        return old_state == new_state

    def propagate_block(self, block: Block):
        for inst in block.code:
            inst.accept(self)

    def unify(self) -> TypedFunc:
        for inst in self.func.func.iter_code():
            if not self.func.defined(inst):
                raise CompilationError(
                    inst.location, f"unknown type {inst}", self._source
                )
        return self.func

    def get_state(self):
        return [type.type for type in self.func.types.values()]

    def literal(self, inst: Literal):
        if not self.first:
            return
        match inst.value:
            case int():
                self.lock_type(inst, int_type, inst.location)
            case float():
                self.lock_type(inst, float_type, inst.location)
            case Func():
                self.lock_type(inst, inst.value.get_type(), inst.location)
            case Struct():
                self.lock_type(inst, StructType(inst.value), inst.location)
            case _:
                raise CompilationError(
                    inst.location, "unsupported literal", self._source
                )

    def load(self, inst: Load) -> None:
        self.copy_type(inst.var, inst, inst.location)

    def store(self, inst: Store) -> None:
        self.lock_type(inst, nothing_type, inst.location)
        self.copy_type(inst.value, inst.var, inst.location)

        def refine(type: Type):
            self.add_type(inst.value, type, inst.location)

        self.refine_map[inst.var] = refine

    def arg(self, inst: Arg) -> None:
        if not self.first:
            return
        assert inst.type is not None
        self.lock_type(inst, inst.type, inst.location)

    def binary(self, inst: Binary) -> None:
        left = self.func.get_type(inst.left)
        right = self.func.get_type(inst.right)
        if not left or not right:
            return
        if type := unify_types((left, right)):
            if inst.op in (
                BinaryOp.ADD,
                BinaryOp.SUB,
                BinaryOp.MUL,
                BinaryOp.DIV,
                BinaryOp.MOD,
            ):
                if not isinstance(type, (IntType, FloatType)):
                    raise CompilationError(
                        inst.location, "operation is not supported", self._source
                    )
            else:
                if not isinstance(type, FloatType):
                    raise CompilationError(
                        inst.location, "operation is not supported", self._source
                    )
            self.add_type(inst, type, inst.location)
        else:
            raise CompilationError(inst.location, "cannot unify types", self._source)

    def unary(self, inst: Unary) -> None:
        type = self.func.get_type(inst.value)
        if not type:
            return
        if inst.op == UnaryOp.NOT:
            self.add_type(inst, bool_type, inst.location)
            if not type.can_convert(bool_type):
                raise TypeError(
                    inst.location, f"cannot convert type {type}", self._source
                )
        elif inst.op == UnaryOp.BIT_NOT:
            if not isinstance(type, IntType):
                raise TypeError(inst.location, "unsupported operation", self._source)
            self.add_type(inst, type, inst.location)
        else:
            if not isinstance(type, (IntType, FloatType)):
                raise TypeError(inst.location, "unsupported operation", self._source)
            self.add_type(inst, type, inst.location)

    def get_block_type_var(self, block: Block) -> Type | None:
        self.propagate_block(block)
        return self.func.get_type(block.code[-1])

    def logical(self, inst: Logical) -> None:
        self.add_type(inst, bool_type, inst.location)
        left = self.func.get_type(inst.left)
        if not left:
            return
        if not left.can_convert(bool_type):
            raise TypeError(inst.location, "cannot convert type", self._source)
        right = self.get_block_type_var(inst.right)
        if not right:
            return
        if not right.can_convert(bool_type):
            raise TypeError(inst.location, "cannot convert type", self._source)

    def comparison(self, inst: Comparison) -> None:
        self.add_type(inst, bool_type, inst.location)
        left = self.func.get_type(inst.left)
        if not left:
            return
        left_type = left
        for comparator in inst.comparators:
            right = self.get_block_type_var(comparator.value)
            if not right:
                return
            right_type = right
            if not unify_types((left_type, right_type)):
                raise TypeError(inst.location, "cannot unify types", self._source)
            left_type = right_type

    def call(self, inst: Call) -> None:
        value = self.func.get_type(inst.value)
        if not value:
            return
        value_type = value
        if isinstance(value_type, FuncType):
            for arg, type in zip(inst.args, value_type.args):
                arg_type = self.func.get_type(arg)
                if not arg_type:
                    return
                arg_type = arg_type
                if not arg_type.can_convert(type):
                    raise TypeError(
                        inst.location,
                        f"cannot convert type {type} to {arg_type}",
                        self._source,
                    )
            self.add_type(inst, value_type.return_type, inst.location)
        elif isinstance(value_type, StructType):
            for arg, field in zip(inst.args, value_type.struct.fields.values()):
                arg_type = self.func.get_type(arg)
                if not arg_type:
                    return
                arg_type = arg_type
                if not arg_type.can_convert(field.type):
                    raise TypeError(inst.location, "cannot convert type", self._source)
            self.add_type(inst, value_type.struct, inst.location)
        else:
            raise TypeError(inst.location, "type not callable", self._source)

    def loop(self, inst: Loop) -> None:
        self.propagate_block(inst.block)
        self.add_type(inst, nothing_type, inst.location)

    def if_inst(self, inst: If) -> None:
        cond_type = self.func.get_type(inst.value)
        if not cond_type:
            return
        if not cond_type.can_convert(bool_type):
            raise TypeError(inst.location, "cannot convert type", self._source)
        self.propagate_block(inst.then_block)
        self.propagate_block(inst.else_block)
        self.add_type(inst, nothing_type, inst.location)

    def return_inst(self, inst: Return) -> None:
        if inst.value is None:
            if self.func.type.return_type != nothing_type:
                raise ValidationError(inst.location, "need return value", self._source)
        else:
            value_type = self.func.get_type(inst.value)
            if not value_type:
                return
            if not value_type.can_convert(self.func.type.return_type):
                raise TypeError(inst.location, "cannot convert type", self._source)
        self.add_type(inst, nothing_type, inst.location)

    def break_inst(self, inst: Break) -> None:
        self.add_type(inst, nothing_type, inst.location)

    def continue_inst(self, inst: Continue) -> None:
        self.add_type(inst, nothing_type, inst.location)

    def address_of(self, inst: AddressOf) -> None:
        type = self.func.get_type(inst.value)
        if not type:
            return
        if isinstance(type, StructType):
            raise TypeError(inst.location, "unsupported operation", self._source)
        if isinstance(type, FuncType):
            raise TypeError(
                inst.location, "func pointer type unsupported", self._source
            )
        self.add_type(inst, PointerType(type), inst.location)

    def get_item(self, inst: GetItem) -> None:
        value_type = self.func.get_type(inst.value)
        index_type = self.func.get_type(inst.index)
        if not value_type or not index_type:
            return
        if not isinstance(value_type, (PointerType, ArrayType)):
            raise TypeError(inst.location, "unsupported get item", self._source)
        if not index_type.can_convert(int_type):
            raise TypeError(
                inst.location, "index type is not converted to int", self._source
            )
        self.add_type(inst, value_type.type, inst.location)

    def var(self, inst: VarDecl) -> None:
        # Lock declared variable types on the first pass
        if not self.first:
            return
        if hasattr(inst, "type") and inst.type is not None:
            self.lock_type(inst, inst.type, inst.location)

    def set_item(self, inst: SetItem) -> None:
        var_type = self.func.get_type(inst.var)
        index_type = self.func.get_type(inst.index)
        value_type = self.func.get_type(inst.value)
        if not var_type or not index_type or not value_type:
            return
        if not isinstance(var_type, (PointerType, ArrayType)):
            raise TypeError(inst.location, "unsupported set item", self._source)
        if not index_type.can_convert(int_type):
            raise TypeError(
                inst.location, "index type is not converted to int", self._source
            )
        if not value_type.can_convert(var_type.type):
            raise TypeError(inst.location, "cannot convert type", self._source)
        self.add_type(inst, nothing_type, inst.location)

    def set_attr(self, inst: SetAttr) -> None:
        var_type = self.func.get_type(inst.var)
        value_type = self.func.get_type(inst.value)
        if not var_type or not value_type:
            return
        if not isinstance(var_type, Struct):
            raise TypeError(
                inst.location, "set attr supported only for structs", self._source
            )
        if field := var_type.fields.get(inst.name):
            if not value_type.can_convert(field.type):
                raise TypeError(inst.location, "cannot convert type", self._source)
            # inst.field = field
            self.add_type(inst, nothing_type, inst.location)
        else:
            raise NameError(inst.location, "field not found", self._source)

    def deref(self, inst: Deref) -> None:
        value_type = self.func.get_type(inst.value)
        if not value_type:
            return
        if not isinstance(value_type, PointerType):
            raise TypeError(
                inst.location, "deref supported only for pointers", self._source
            )
        self.add_type(inst, value_type.type, inst.location)

    def array(self, inst: Array) -> None:
        # Infer array element type from items
        if not inst.items:
            raise ValidationError(inst.location, "empty array literal", self._source)
        types = []
        for item in inst.items:
            type = self.func.get_type(item)
            if not type:
                return
            types.append(type)
        unified = unify_types(types)
        if unified is None:
            raise TypeError(
                inst.location, "cannot unify array element types", self._source
            )
        self.add_type(inst, ArrayType(unified, len(inst.items)), inst.location)

    def get_attr(self, inst: GetAttr) -> None:
        value_type = self.func.get_type(inst.value)
        if not value_type:
            return
        type = value_type
        if not isinstance(type, Struct):
            raise TypeError(
                inst.location, "get attr supported only from structs", self._source
            )
        if field := type.fields.get(inst.name):
            self.add_type(inst, field.type, inst.location)
        else:
            raise NameError(inst.location, "field not found", self._source)

    def as_inst(self, inst: AsInst) -> None:
        value_type = self.func.get_type(inst.value)
        if not value_type:
            return
        if not value_type.can_explicit_convert(inst.type):
            raise TypeError(inst.location, "cannot convert type", self._source)
        self.add_type(inst, inst.type, inst.location)
