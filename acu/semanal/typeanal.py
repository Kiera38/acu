from __future__ import annotations

from collections.abc import Callable, Iterable

from acu.semanal.ir import (
    AddressOf,
    Arg,
    Array,
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
    int_type,
    float_type,
    bool_type,
    nothing_type,
    unify_types,
)
from acu.source import Location


class TypeAnalyzer(InstVisitor[None]):
    def __init__(self, func: Func) -> None:
        super().__init__()
        self.func = TypedFunc(func, func.get_type())
        self.refine_map: dict[Inst, Callable[[Type], None]] = {}
        self.first = True

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
                raise Exception(f"unknown type {inst}")
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
                raise Exception("unsupported literal")

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
                    raise Exception("operation is not supported")
            else:
                if not isinstance(type, FloatType):
                    raise Exception("operation is not supported")
            self.add_type(inst, type, inst.location)
        else:
            raise Exception("cannot unify types")

    def unary(self, inst: Unary) -> None:
        type = self.func.get_type(inst.value)
        if not type:
            return
        if inst.op == UnaryOp.NOT:
            self.add_type(inst, bool_type, inst.location)
            if not type.can_convert(bool_type):
                raise Exception(f"cannot convert type {type}")
        elif inst.op == UnaryOp.BIT_NOT:
            if not isinstance(type, IntType):
                raise Exception("unsupported operation")
            self.add_type(inst, type, inst.location)
        else:
            if not isinstance(type, (IntType, FloatType)):
                raise Exception("unsupported operation")
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
            raise Exception("cannot convert type")
        right = self.get_block_type_var(inst.right)
        if not right:
            return
        if not right.can_convert(bool_type):
            raise Exception("cannot convert type")

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
                raise Exception("cannot unify types")
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
                    raise Exception(f"cannot convert type {type} to {arg_type}")
            self.add_type(inst, value_type.return_type, inst.location)
        elif isinstance(value_type, StructType):
            for arg, field in zip(inst.args, value_type.struct.fields.values()):
                arg_type = self.func.get_type(arg)
                if not arg_type:
                    return
                arg_type = arg_type
                if not arg_type.can_convert(field.type):
                    raise Exception("cannot convert type")
            self.add_type(inst, value_type.struct, inst.location)
        else:
            raise Exception("type not callable")

    def loop(self, inst: Loop) -> None:
        self.propagate_block(inst.block)
        self.add_type(inst, nothing_type, inst.location)

    def if_inst(self, inst: If) -> None:
        cond_type = self.func.get_type(inst.value)
        if not cond_type:
            return
        if not cond_type.can_convert(bool_type):
            raise Exception("cannot convert type")
        self.propagate_block(inst.then_block)
        self.propagate_block(inst.else_block)
        self.add_type(inst, nothing_type, inst.location)

    def return_inst(self, inst: Return) -> None:
        if inst.value is None:
            if self.func.type.return_type != nothing_type:
                raise Exception("need return value")
        else:
            value_type = self.func.get_type(inst.value)
            if not value_type:
                return
            if not value_type.can_convert(self.func.type.return_type):
                raise Exception("cannot convert type")
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
            raise Exception("unsupported operation")
        if isinstance(type, FuncType):
            raise Exception("func pointer type unsupported")
        self.add_type(inst, PointerType(type), inst.location)

    def get_item(self, inst: GetItem) -> None:
        value_type = self.func.get_type(inst.value)
        index_type = self.func.get_type(inst.index)
        if not value_type or not index_type:
            return
        if not isinstance(value_type, (PointerType, ArrayType)):
            raise Exception("unsupported get item")
        if not index_type.can_convert(int_type):
            raise Exception("index type is not converted to int")
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
            raise Exception("unsupported set item")
        if not index_type.can_convert(int_type):
            raise Exception("index type is not converted to int")
        if not value_type.can_convert(var_type.type):
            raise Exception("cannot convert type")
        self.add_type(inst, nothing_type, inst.location)

    def set_attr(self, inst: SetAttr) -> None:
        var_type = self.func.get_type(inst.var)
        value_type = self.func.get_type(inst.value)
        if not var_type or not value_type:
            return
        if not isinstance(var_type, Struct):
            raise Exception("set attr supported only for structs")
        if field := var_type.fields.get(inst.name):
            if not value_type.can_convert(field.type):
                raise Exception("cannot convert type")
            # inst.field = field
            self.add_type(inst, nothing_type, inst.location)
        else:
            raise Exception("field not found")

    def deref(self, inst: Deref) -> None:
        value_type = self.func.get_type(inst.value)
        if not value_type:
            return
        if not isinstance(value_type, PointerType):
            raise Exception("deref supported only for pointers")
        self.add_type(inst, value_type.type, inst.location)

    def array(self, inst: Array) -> None:
        # Infer array element type from items
        if not inst.items:
            raise Exception("empty array literal")
        types = []
        for item in inst.items:
            type = self.func.get_type(item)
            if not type:
                return
            types.append(type)
        unified = unify_types(types)
        if unified is None:
            raise Exception("cannot unify array element types")
        self.add_type(inst, ArrayType(unified, len(inst.items)), inst.location)

    def get_attr(self, inst: GetAttr) -> None:
        value_type = self.func.get_type(inst.value)
        if not value_type:
            return
        type = value_type
        if not isinstance(type, Struct):
            raise Exception("get attr supported only from structs")
        if field := type.fields.get(inst.name):
            self.add_type(inst, field.type, inst.location)
        else:
            raise Exception("field not found")
