from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import TYPE_CHECKING, Any, cast

from acu.errors import CompilationError
from acu.parser import Location
from acu.semanal.types import FuncType, Struct, StructField, Type
from acu.source import Source


class InstVisitor[T]:
    def inst(self, inst: Inst) -> T:  # type: ignore
        pass

    def literal(self, inst: Literal) -> T:
        return self.inst(inst)

    def var(self, inst: VarDecl) -> T:
        return self.inst(inst)

    def arg(self, inst: Arg) -> T:
        return self.inst(inst)

    def store(self, inst: Store) -> T:
        return self.inst(inst)

    def load(self, inst: Load) -> T:
        return self.inst(inst)

    def binary(self, inst: Binary) -> T:
        return self.inst(inst)

    def logical(self, inst: Logical) -> T:
        return self.inst(inst)

    def unary(self, inst: Unary) -> T:
        return self.inst(inst)

    def comparison(self, inst: Comparison) -> T:
        return self.inst(inst)

    def call(self, inst: Call) -> T:
        return self.inst(inst)

    def loop(self, inst: Loop) -> T:
        return self.inst(inst)

    def if_inst(self, inst: If) -> T:
        return self.inst(inst)

    def return_inst(self, inst: Return) -> T:
        return self.inst(inst)

    def break_inst(self, inst: Break) -> T:
        return self.inst(inst)

    def continue_inst(self, inst: Continue) -> T:
        return self.inst(inst)

    def address_of(self, inst: AddressOf) -> T:
        return self.inst(inst)

    def get_item(self, inst: GetItem) -> T:
        return self.inst(inst)

    def set_item(self, inst: SetItem) -> T:
        return self.inst(inst)

    def get_attr(self, inst: GetAttr) -> T:
        return self.inst(inst)

    def set_attr(self, inst: SetAttr) -> T:
        return self.inst(inst)

    def deref(self, inst: Deref) -> T:
        return self.inst(inst)

    def array(self, inst: Array) -> T:
        return self.inst(inst)

    def as_inst(self, inst: AsInst) -> T:
        return self.inst(inst)


@dataclass(eq=False)
class Inst:
    location: Location

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.inst(self)


@dataclass(eq=False)
class Block:
    code: list[Inst]


@dataclass(eq=False)
class Literal(Inst):
    value: bool | int | float | str | Func | Struct | Any

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.literal(self)


@dataclass(eq=False)
class VarDecl(Inst):
    name: str
    type: Type | None = None

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.var(self)


@dataclass(eq=False)
class Arg(Inst):
    name: str
    type: Type | None = None

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.arg(self)


@dataclass(eq=False)
class Store(Inst):
    var: Inst
    value: Inst

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.store(self)


@dataclass(eq=False)
class Load(Inst):
    var: Inst

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.load(self)


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


@dataclass(eq=False)
class Binary(Inst):
    left: Inst
    right: Inst
    op: BinaryOp

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.binary(self)


class LogicalOp(Enum):
    AND = auto()
    OR = auto()


@dataclass(eq=False)
class Logical(Inst):
    left: Inst
    right: Block
    op: LogicalOp

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.logical(self)


class UnaryOp(Enum):
    NOT = auto()
    NEG = auto()
    BIT_NOT = auto()


@dataclass(eq=False)
class Unary(Inst):
    value: Inst
    op: UnaryOp

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.unary(self)


class ComparisonOp(Enum):
    LESS = auto()
    GREATER = auto()
    LESS_EQUAL = auto()
    GREATER_EQUAL = auto()
    EQUAL = auto()
    NOT_EQUAL = auto()


@dataclass(eq=False)
class Comparator:
    value: Block
    op: ComparisonOp


@dataclass(eq=False)
class Comparison(Inst):
    left: Inst
    comparators: list[Comparator]

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.comparison(self)


@dataclass(eq=False)
class Call(Inst):
    value: Inst
    args: list[Inst]

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.call(self)


@dataclass(eq=False)
class Loop(Inst):
    block: Block

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.loop(self)


@dataclass(eq=False)
class If(Inst):
    value: Inst
    then_block: Block
    else_block: Block

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.if_inst(self)


@dataclass(eq=False)
class Return(Inst):
    value: Inst | None

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.return_inst(self)


@dataclass(eq=False)
class Break(Inst):
    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.break_inst(self)


@dataclass(eq=False)
class Continue(Inst):
    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.continue_inst(self)


@dataclass(eq=False)
class AddressOf(Inst):
    value: Inst

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.address_of(self)


@dataclass(eq=False)
class GetItem(Inst):
    value: Inst
    index: Inst

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.get_item(self)


@dataclass(eq=False)
class SetItem(Inst):
    var: Inst
    index: Inst
    value: Inst

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.set_item(self)


@dataclass(eq=False)
class GetAttr(Inst):
    value: Inst
    name: str
    field: StructField = field(init=False)

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.get_attr(self)


@dataclass(eq=False)
class SetAttr(Inst):
    var: Inst
    value: Inst
    name: str
    field: StructField = field(init=False)

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.set_attr(self)


@dataclass(eq=False)
class Deref(Inst):
    value: Inst

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.deref(self)


@dataclass(eq=False)
class Array(Inst):
    items: list[Inst]

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.array(self)


@dataclass(eq=False)
class AsInst(Inst):
    value: Inst
    type: Type

    def accept[T](self, visitor: InstVisitor[T]) -> T:
        return visitor.as_inst(self)


@dataclass(eq=False)
class Func:
    name: str
    arg_count: int
    return_type: Type
    code: Block
    location: Location

    def get_type(self):
        arg_types = []
        for arg in self.code.code[: self.arg_count]:
            assert isinstance(arg, Arg)
            assert cast(Arg, arg).type is not None
            arg_types.append(arg.type)
        return FuncType(arg_types, self.return_type)

    def __hash__(self) -> int:
        return id(self)

    def iter_code(self):
        return CodeIterator(self.code)
    

@dataclass(eq=False)
class UsedFunc:
    func: Func
    module: Module


@dataclass
class Module:
    source: Source
    funcs: list[Func]
    structs: list[Struct]
    items: dict[str, Func | Struct]

    def find(self, name: str, location: Location):
        if item := self.items.get(name):
            if isinstance(item, Func):
                return UsedFunc(item, self)
            return item
        raise CompilationError(
            location, f"module not contain item '{name}'", self.source
        )


class CodeIterator(Iterable, InstVisitor[Iterator[Inst]]):
    def __init__(self, code: Block) -> None:
        super().__init__()
        self.code = code

    def __iter__(self) -> Iterator:
        for inst in self.code.code:
            yield from inst.accept(self)

    def inst(self, inst: Inst) -> Iterator[Inst]:
        yield inst

    def if_inst(self, inst: If) -> Iterator[Inst]:
        yield inst
        yield from CodeIterator(inst.then_block)
        yield from CodeIterator(inst.else_block)

    def loop(self, inst: Loop) -> Iterator[Inst]:
        yield inst
        yield from CodeIterator(inst.block)

    def comparison(self, inst: Comparison) -> Iterator[Inst]:
        yield inst
        for comparator in inst.comparators:
            yield from CodeIterator(comparator.value)

    def logical(self, inst: Logical) -> Iterator[Inst]:
        yield inst
        yield from CodeIterator(inst.right)
