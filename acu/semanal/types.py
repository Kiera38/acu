from __future__ import annotations

from ast import In
from dataclasses import dataclass, field
from functools import cached_property
from typing import TYPE_CHECKING, Iterable

from acu.source import Location

if TYPE_CHECKING:
    from acu.semanal.ir import Inst
    from acu.semanal.ir import Func


@dataclass
class Type:
    def can_convert(self, to: Type) -> bool:
        return self == to

    def can_explicit_convert(self, to: Type) -> bool:
        return self.can_convert(to)


@dataclass
class NothingType(Type):
    pass


nothing_type = NothingType()


@dataclass
class BoolType(Type):
    def can_convert(self, to: Type) -> bool:
        return super().can_convert(to) or isinstance(to, (IntType, FloatType))


bool_type = BoolType()


@dataclass
class IntType(Type):
    size: int
    signed: bool = True

    def can_convert(self, to: Type) -> bool:
        return (
            super().can_convert(to)
            or isinstance(to, FloatType)
            or (
                isinstance(to, IntType)
                and (
                    (to.signed == self.signed and to.size >= self.size)
                    or (not self.signed and to.signed and to.size > self.size)
                )
            )
            or isinstance(to, BoolType)
        )

    def can_explicit_convert(self, to: Type) -> bool:
        return (
            self.can_convert(to)
            or isinstance(to, IntType)
            or isinstance(to, PointerType)
            and self.size == 64
        )


int_type = IntType(64)
int8_type = IntType(8)
int16_type = IntType(16)
int32_type = IntType(32)
int64_type = IntType(64)

uint_type = IntType(64, signed=False)
uint8_type = IntType(8, signed=False)
uint16_type = IntType(16, signed=False)
uint32_type = IntType(32, signed=False)
uint64_type = IntType(64, signed=False)


@dataclass
class FloatType(Type):
    size: int

    def can_convert(self, to: Type) -> bool:
        return (
            super().can_convert(to)
            or isinstance(to, FloatType)
            and to.size >= self.size
            or isinstance(to, BoolType)
        )

    def can_explicit_convert(self, to: Type) -> bool:
        return self.can_convert(to) or isinstance(to, IntType)


float_type = FloatType(64)
float32_type = FloatType(32)
float64_type = FloatType(64)


@dataclass
class FuncType(Type):
    args: list[Type]
    return_type: Type


@dataclass
class ArrayType(Type):
    type: Type
    size: int

    def can_explicit_convert(self, to: Type) -> bool:
        return super().can_explicit_convert(to) or to == PointerType(self.type)


@dataclass
class PointerType(Type):
    type: Type

    def can_explicit_convert(self, to: Type) -> bool:
        return super().can_explicit_convert(to) or to == int_type or to == uint_type


@dataclass
class StructField:
    type: Type
    index: int
    location: Location


@dataclass
class Struct(Type):
    name: str
    fields: dict[str, StructField]
    location: Location

    @cached_property
    def field_list(self):
        return sorted(self.fields.items(), key=lambda f: f[1].index)


@dataclass
class StructType(Type):
    struct: Struct


@dataclass
class TypeInfo:
    type: Type
    location: Location
    locked: bool = False


def unify_types(types: Iterable[Type]):
    for first in types:
        if all(second.can_convert(first) for second in types):
            return first
    return None


@dataclass
class TypedFunc:
    func: Func
    type: FuncType
    types: dict[Inst, TypeInfo] = field(default_factory=dict)

    def add_type(self, inst: Inst, type: Type, location: Location):
        info = self.types.get(inst)
        if not info:
            self.types[inst] = TypeInfo(type, location)
            return
        if info.locked:
            if type != info.type:
                assert info.type is not None
                if not type.can_convert(info.type):
                    raise Exception("cannot convert type")
        else:
            unified = unify_types((info.type, type))
            if unified is None:
                raise Exception("connot unify types")
            info.type = unified
        return info.type

    def inst_type(self, inst: Inst) -> Type:
        return self.types[inst].type

    def get_type(self, inst: Inst) -> Type | None:
        if info := self.types.get(inst):
            return info.type
        return None

    def defined(self, inst: Inst) -> bool:
        return inst in self.types

    def lock_type(self, inst: Inst, type: Type, location: Location):
        info = self.types.get(inst)
        if not info:
            info = self.types[inst] = TypeInfo(type, location, True)
        else:
            if not info.type.can_convert(type):
                raise Exception("no conversion type")
            info.locked = True
            info.type = type
            if not info.location:
                info.location = location

    def union(self, inst_to: Inst, inst: Inst, location: Location):
        if info := self.types.get(inst):
            self.add_type(inst_to, info.type, location)
