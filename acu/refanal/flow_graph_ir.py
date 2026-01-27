from __future__ import annotations

from abc import abstractmethod
from collections.abc import Sequence
from dataclasses import dataclass, field
from enum import Enum, auto

from acu.semanal.ir import BinaryOp, ComparisonOp, UnaryOp
from acu.semanal.types import (
    ArrayType,
    PointerType,
    Struct,
    Type,
    bool_type,
    float_type,
    int_type,
    nothing_type,
)
from acu.source import Location


@dataclass(eq=False)
class BasicBlock:
    label: int = -1
    ops: list[Op] = field(default_factory=list)

    @property
    def terminated(self) -> bool:
        return bool(self.ops) and isinstance(self.ops[-1], ControlOp)

    @property
    def terminator(self) -> ControlOp:
        assert self.ops
        assert isinstance(self.ops[-1], ControlOp)
        return self.ops[-1]


class Specifier(Enum):
    VAR = auto()
    LET = auto()
    VAL = auto()


class Value:
    location: Location
    type: Type = nothing_type
    specifier: Specifier | None = None

    @property
    def is_void(self) -> bool:
        return self.type == nothing_type


class Register(Value):
    def __init__(
        self,
        location: Location,
        type: Type,
        specifier: Specifier = Specifier.VAR,
        name: str = "",
        is_arg: bool = False,
    ):
        self.location = location
        self.type = type
        self.specifier = specifier
        self.name = name
        self.is_arg = is_arg

    def __repr__(self):
        return f"<Register {self.name!r} at {hex(id(self))}>"


class Boolean(Value):
    def __init__(self, location: Location, value: bool):
        self.value = value
        self.location = location
        self.type = bool_type
        self.specifier = Specifier.VAL


class Integer(Value):
    def __init__(self, location: Location, value: int):
        self.value = value
        self.location = location
        self.type = int_type
        self.specifier = Specifier.VAL


class Float(Value):
    def __init__(self, location: Location, value: float):
        self.value = value
        self.location = location
        self.type = float_type
        self.specifier = Specifier.VAL


class Undef(Value):
    def __init__(self, type: Type):
        self.type = type
        self.specifier = Specifier.VAL


class Op(Value):
    def __init__(self, location: Location):
        self.location = location

    @abstractmethod
    def sources(self) -> list[Value]:
        pass

    @abstractmethod
    def set_sources(self, new: list[Value]) -> None:
        pass

    def unique_sources(self) -> list[Value]:
        result = []
        for reg in self.sources():
            if reg not in result:
                result.append(reg)
        return result

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.op(self)


class Store(Op):
    def __init__(self, location: Location, dest: Value, src: Value):
        super().__init__(location)
        self.dest = dest
        self.src = src

    def sources(self) -> list[Value]:
        return [self.src]

    def set_sources(self, new: list[Value]):
        (self.src,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.store(self)


class ControlOp(Op):
    def targets(self) -> Sequence[BasicBlock]:
        return ()

    def set_target(self, i: int, new: BasicBlock) -> None:
        raise AssertionError(f"invalid set_target({self}, {i})")

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.control_op(self)


class Goto(ControlOp):
    def __init__(self, location: Location, label: BasicBlock):
        super().__init__(location)
        self.label = label

    def targets(self) -> Sequence[BasicBlock]:
        return (self.label,)

    def set_target(self, i: int, new: BasicBlock) -> None:
        assert i == 0
        self.label = new

    def sources(self) -> list[Value]:
        return []

    def set_sources(self, new: list[Value]) -> None:
        assert not new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.goto(self)


class Branch(ControlOp):
    def __init__(
        self, location: Location, value: Value, true: BasicBlock, false: BasicBlock
    ):
        super().__init__(location)
        self.value = value
        self.true_label = true
        self.false_label = false

    def targets(self) -> Sequence[BasicBlock]:
        return (self.true_label, self.false_label)

    def set_target(self, i: int, new: BasicBlock) -> None:
        assert i == 0 or i == 1
        if i == 0:
            self.true_label = new
        else:
            self.false_label = new

    def sources(self) -> list[Value]:
        return [self.value]

    def set_sources(self, new: list[Value]) -> None:
        (self.value,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.branch(self)


class Return(ControlOp):
    def __init__(self, location: Location, value: Value):
        super().__init__(location)
        self.value = value

    def sources(self) -> list[Value]:
        return [self.value]

    def set_sources(self, new: list[Value]) -> None:
        (self.value,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.visit_return(self)


class Unreachable(ControlOp):
    def sources(self) -> list[Value]:
        return []

    def set_sources(self, new: list[Value]) -> None:
        assert not new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.unreachable(self)


class RegisterOp(Op):
    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.register_op(self)


class Call(RegisterOp):
    def __init__(self, location: Location, fn: FuncIR, args: Sequence[Value]):
        super().__init__(location)
        self.fn = fn
        # assert len(args) == len(fn.args), f"expected {len(fn.args)} args, got {len(args)} in call to {fn.name}"
        self.args = list(args)
        self.type = fn.return_type
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return self.args.copy()

    def set_sources(self, new: list[Value]) -> None:
        self.args = new[:]

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.call(self)


class GetField(RegisterOp):
    def __init__(self, location: Location, obj: Value, field: int):
        super().__init__(location)
        assert isinstance(obj.type, Struct)
        assert len(obj.type.fields) >= field
        self.obj = obj
        self.field = field
        self.type = obj.type.field_list[field][1].type
        self.specifier = Specifier.VAR

    def sources(self) -> list[Value]:
        return [self.obj]

    def set_sources(self, new: list[Value]) -> None:
        (self.obj,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.get_field(self)


class SetField(RegisterOp):
    def __init__(self, location: Location, obj: Value, field: int, value: Value):
        super().__init__(location)
        assert isinstance(obj.type, Struct)
        assert len(obj.type.fields) >= field
        assert obj.type.field_list[field][1].type == value.type
        self.obj = obj
        self.field = field
        self.value = value

    def sources(self) -> list[Value]:
        return [self.obj, self.value]

    def set_sources(self, new: list[Value]) -> None:
        (self.obj, self.value) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.set_field(self)


class GetItem(RegisterOp):
    def __init__(self, location: Location, obj: Value, index: Value):
        super().__init__(location)
        self.obj = obj
        self.index = index
        assert isinstance(obj.type, (ArrayType, PointerType))
        self.type = obj.type.type
        self.specifier = Specifier.VAR

    def sources(self) -> list[Value]:
        return [self.obj, self.index]

    def set_sources(self, new: list[Value]) -> None:
        (self.obj, self.index) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.get_item(self)


class SetItem(RegisterOp):
    def __init__(self, location: Location, obj: Value, index: Value, value: Value):
        super().__init__(location)
        assert isinstance(obj.type, (ArrayType, PointerType))
        assert obj.type.type == value.type
        self.obj = obj
        self.index = index
        self.value = value

    def sources(self) -> list[Value]:
        return [self.obj, self.index, self.value]

    def set_sources(self, new: list[Value]) -> None:
        (self.obj, self.index, self.value) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.set_item(self)


class Cast(RegisterOp):
    def __init__(self, location: Location, type: Type, obj: Value):
        super().__init__(location)
        self.type = type
        self.obj = obj
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return [self.obj]

    def set_sources(self, new: list[Value]) -> None:
        (self.obj,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.cast(self)


class GetAddress(RegisterOp):
    def __init__(self, location: Location, src: Value):
        super().__init__(location)
        self.src = src
        self.type = PointerType(src.type)
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return [self.src]

    def set_sources(self, new: list[Value]) -> None:
        (self.src,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.get_address(self)


class Deref(RegisterOp):
    def __init__(self, location: Location, ptr: Value):
        super().__init__(location)
        self.ptr = ptr
        assert isinstance(ptr.type, PointerType)
        self.type = ptr.type.type
        self.specifier = Specifier.VAR

    def sources(self) -> list[Value]:
        return [self.ptr]

    def set_sources(self, new: list[Value]) -> None:
        (self.ptr,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.deref(self)


class Binary(RegisterOp):
    def __init__(self, location: Location, left: Value, right: Value, op: BinaryOp):
        super().__init__(location)
        self.left = left
        self.right = right
        self.op = op
        self.specifier = Specifier.VAL
        self.type = left.type

    def sources(self) -> list[Value]:
        return [self.left, self.right]

    def set_sources(self, new: list[Value]) -> None:
        (self.left, self.right) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.binary(self)


class Unary(RegisterOp):
    def __init__(self, location: Location, value: Value, op: UnaryOp):
        super().__init__(location)
        self.value = value
        self.op = op
        self.type = value.type
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return [self.value]

    def set_sources(self, new: list[Value]) -> None:
        (self.value,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.unary(self)


class Comparison(RegisterOp):
    def __init__(self, location: Location, left: Value, right: Value, op: ComparisonOp):
        super().__init__(location)
        self.left = left
        self.right = right
        self.op = op
        self.type = bool_type
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return [self.left, self.right]

    def set_sources(self, new: list[Value]) -> None:
        (self.left, self.right) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.comparison(self)


class Array(RegisterOp):
    def __init__(self, location: Location, items: Sequence[Value]):
        super().__init__(location)
        self.items = list(items)
        self.type = ArrayType(items[0].type, len(items))
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return self.items.copy()

    def set_sources(self, new: list[Value]) -> None:
        self.items = new[:]

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.array(self)


class CreateStruct(RegisterOp):
    def __init__(self, location: Location, fields: Sequence[Value], type: Struct):
        super().__init__(location)
        self.type = type
        self.fields = list(fields)
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return self.fields.copy()

    def set_sources(self, new: list[Value]) -> None:
        self.fields = new[:]

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.create_struct(self)


class GetPtr(RegisterOp):
    def __init__(self, location: Location, src: Register):
        super().__init__(location)
        self.src = src
        self.type = PointerType(src.type)
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return [self.src]

    def set_sources(self, new: list[Value]) -> None:
        (self.src,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.get_ptr(self)


class LoadPtr(RegisterOp):
    def __init__(self, location: Location, ptr: Value):
        super().__init__(location)
        assert isinstance(ptr.type, PointerType)
        self.ptr = ptr
        self.type = ptr.type.type
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return [self.ptr]

    def set_sources(self, new: list[Value]) -> None:
        (self.ptr,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.load_ptr(self)


class StorePtr(RegisterOp):
    def __init__(self, location: Location, ptr: Value, value: Value):
        super().__init__(location)
        assert isinstance(ptr.type, PointerType)
        assert ptr.type.type == value.type
        self.ptr = ptr
        self.value = value

    def sources(self) -> list[Value]:
        return [self.ptr, self.value]

    def set_sources(self, new: list[Value]) -> None:
        (self.ptr, self.value) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.store_ptr(self)


class GetItemPtr(RegisterOp):
    def __init__(self, location: Location, ptr: Value, index: Value):
        super().__init__(location)
        assert isinstance(ptr.type, PointerType)
        assert isinstance(ptr.type.type, (ArrayType, PointerType))
        self.ptr = ptr
        self.index = index
        self.type = PointerType(ptr.type.type.type)
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return [self.ptr, self.index]

    def set_sources(self, new: list[Value]) -> None:
        (self.ptr, self.index) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.get_item_ptr(self)


class GetFieldPtr(RegisterOp):
    def __init__(self, location: Location, ptr: Value, field: int):
        super().__init__(location)
        assert isinstance(ptr.type, PointerType)
        assert isinstance(ptr.type.type, Struct)
        self.ptr = ptr
        self.field = field
        self.type = PointerType(ptr.type.type.field_list[field][1].type)
        self.specifier = Specifier.VAL

    def sources(self) -> list[Value]:
        return [self.ptr]

    def set_sources(self, new: list[Value]) -> None:
        (self.ptr,) = new

    def accept[T](self, visitor: OpVisitor[T]) -> T:
        return visitor.get_field_ptr(self)


@dataclass(eq=False)
class FuncIR:
    location: Location
    name: str
    args: list[Register]
    blocks: list[BasicBlock]
    return_type: Type = field(default_factory=lambda: nothing_type)


class OpVisitor[T]:
    def op(self, op: Op) -> T:  # type: ignore
        pass

    def store(self, op: Store) -> T:
        return self.op(op)

    def control_op(self, op: ControlOp) -> T:
        return self.op(op)

    def goto(self, op: Goto) -> T:
        return self.control_op(op)

    def branch(self, op: Branch) -> T:
        return self.control_op(op)

    def visit_return(self, op: Return) -> T:
        return self.control_op(op)

    def unreachable(self, op: Unreachable) -> T:
        return self.control_op(op)

    def register_op(self, op: RegisterOp) -> T:
        return self.op(op)

    def call(self, op: Call) -> T:
        return self.register_op(op)

    def get_field(self, op: GetField) -> T:
        return self.register_op(op)

    def set_field(self, op: SetField) -> T:
        return self.register_op(op)

    def get_item(self, op: GetItem) -> T:
        return self.register_op(op)

    def set_item(self, op: SetItem) -> T:
        return self.register_op(op)

    def cast(self, op: Cast) -> T:
        return self.register_op(op)

    def get_address(self, op: GetAddress) -> T:
        return self.register_op(op)

    def deref(self, op: Deref) -> T:
        return self.register_op(op)

    def binary(self, op: Binary) -> T:
        return self.register_op(op)

    def unary(self, op: Unary) -> T:
        return self.register_op(op)

    def comparison(self, op: Comparison) -> T:
        return self.register_op(op)

    def array(self, op: Array) -> T:
        return self.register_op(op)

    def create_struct(self, op: CreateStruct) -> T:
        return self.register_op(op)

    def get_ptr(self, op: GetPtr) -> T:
        return self.register_op(op)

    def load_ptr(self, op: LoadPtr) -> T:
        return self.register_op(op)

    def store_ptr(self, op: StorePtr) -> T:
        return self.register_op(op)

    def get_item_ptr(self, op: GetItemPtr) -> T:
        return self.register_op(op)

    def get_field_ptr(self, op: GetFieldPtr) -> T:
        return self.register_op(op)
