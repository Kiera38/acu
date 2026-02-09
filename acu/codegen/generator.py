import llvmlite.ir as llir

from acu.refanal.flow_graph_ir import (
    Array,
    BasicBlock,
    Binary,
    Boolean,
    Branch,
    Call,
    Cast,
    Comparison,
    CreateStruct,
    Float,
    FuncIR,
    GetFieldPtr,
    GetItemPtr,
    GetPtr,
    Goto,
    Integer,
    LoadPtr,
    Op,
    OpVisitor,
    Register,
    Return,
    StorePtr,
    Unary,
    Unreachable,
    Value,
)
from acu.semanal.ir import BinaryOp, ComparisonOp, UnaryOp
from acu.semanal.types import (
    ArrayType,
    PointerType,
    Struct,
    Type,
    BoolType,
    FloatType,
    IntType,
    NothingType,
)


class LLVMGenerator(OpVisitor[llir.Value]):
    def __init__(self, module: llir.Module) -> None:
        self.block_map: dict[BasicBlock, llir.Block] = {}
        self.value_map: dict[Value, llir.Value] = {}
        self.module = module
        self.builder = llir.IRBuilder()
        self.funcs: dict[FuncIR, llir.Function] = {}
        self.const_int_type = llir.IntType(32)

    def int_const(self, value: int, type: IntType | None = None) -> llir.Value:
        if type is not None:
            return llir.IntType(type.size)(value)
        return self.const_int_type(value)

    def type(self, type: Type) -> llir.Type:
        match type:
            case BoolType():
                return llir.IntType(1)
            case FloatType() if type.size == 64:
                return llir.DoubleType()
            case FloatType() if type.size == 32:
                return llir.FloatType()
            case IntType(size=size):
                return llir.IntType(size)
            case NothingType():
                return llir.VoidType()
            case PointerType():
                return llir.PointerType()
            case ArrayType():
                return llir.ArrayType(self.type(type.type), type.size)
            case Struct():
                return llir.LiteralStructType(
                    [self.type(field[1].type) for field in type.field_list]
                )
            case _:
                raise Exception("unknown type")

    def create(self, func: FuncIR):
        self.funcs[func] = llir.Function(
            self.module,
            llir.FunctionType(
                self.type(func.return_type), [self.type(arg.type) for arg in func.args]
            ),
            func.qual_name,
        )

    def generate(self, func: FuncIR):
        self.block_map.clear()
        self.value_map.clear()
        ll_func = self.funcs[func]
        for block in func.blocks:
            self.block_map[block] = ll_func.append_basic_block()

        self.builder.position_at_end(ll_func.entry_basic_block)
        for arg, ll_arg in zip(func.args, ll_func.args):
            ptr = self.builder.alloca(ll_arg.type)
            self.builder.store(ll_arg, ptr)
            self.value_map[arg] = ptr

        for block, ll_block in self.block_map.items():
            self.builder.position_at_end(ll_block)
            for op in block.ops:
                value = op.accept(self)
                self.value_map[op] = value

    def block(self, block: BasicBlock) -> llir.Block:
        return self.block_map[block]

    def value(self, value: Value) -> llir.Value:
        if ll_value := self.value_map.get(value):
            if isinstance(value, Register):
                return self.builder.load(ll_value)
            return ll_value
        if isinstance(value, (Boolean, Integer, Float)):
            ll_value = self.type(value.type)(value.value)
        elif isinstance(value, Register):
            ll_value = self.builder.alloca(self.type(value.type))
        else:
            raise Exception("unknown value")
        self.value_map[value] = ll_value
        if isinstance(value, Register):
            return self.builder.load(ll_value)
        return ll_value

    def get_ptr(self, op: GetPtr) -> llir.Value:
        ll_value = self.value_map.get(op.src)
        if ll_value is None:
            ll_value = self.builder.alloca(self.type(op.src.type))
            self.value_map[op.src] = ll_value
        return ll_value

    def load_ptr(self, op: LoadPtr) -> llir.Value:
        return self.builder.load(self.value(op.ptr), typ=self.type(op.type))

    def store_ptr(self, op: StorePtr) -> llir.Value:
        return self.builder.store(self.value(op.value), self.value(op.ptr))

    def get_field_ptr(self, op: GetFieldPtr) -> llir.Value:
        assert isinstance(op.ptr.type, PointerType)
        return self.builder.gep(
            self.value(op.ptr),
            [self.int_const(0), self.int_const(op.field)],
            source_etype=self.type(op.ptr.type.type),
        )

    def get_item_ptr(self, op: GetItemPtr) -> llir.Value:
        assert isinstance(op.ptr.type, PointerType)
        if isinstance(op.ptr.type.type, ArrayType):
            return self.builder.gep(
                self.value(op.ptr),
                [self.int_const(0), self.value(op.index)],
                source_etype=self.type(op.ptr.type.type),
            )
        assert isinstance(op.ptr.type.type, PointerType)
        value = self.builder.load(self.value(op.ptr), typ=self.type(op.ptr.type.type))
        return self.builder.gep(
            value,
            [self.value(op.index)],
            source_etype=self.type(op.ptr.type.type.type),
        )

    def goto(self, op: Goto) -> llir.Value:
        return self.builder.branch(self.block(op.label))

    def branch(self, op: Branch) -> llir.Value:
        return self.builder.cbranch(
            self.value(op.value), self.block(op.true_label), self.block(op.false_label)
        )

    def visit_return(self, op: Return) -> llir.Value:
        if op.value.is_void:
            return self.builder.ret_void()
        return self.builder.ret(self.value(op.value))

    def unreachable(self, op: Unreachable) -> llir.Value:
        return self.builder.unreachable()

    def func(self, func: FuncIR) -> llir.Function:
        return self.funcs[func]

    def call(self, op: Call) -> llir.Value:
        return self.builder.call(self.func(op.fn), [self.value(arg) for arg in op.args])

    def cast(self, op: Cast) -> llir.Value:
        match op.type:
            case BoolType():
                match op.obj.type:
                    case IntType(signed=True):
                        return self.builder.icmp_signed(
                            "!=", self.value(op.obj), self.int_const(0, op.obj.type)
                        )
                    case IntType(signed=False):
                        return self.builder.icmp_unsigned(
                            "!=", self.value(op.obj), self.int_const(0, op.obj.type)
                        )
                    case FloatType():
                        return self.builder.fcmp_unordered(
                            "!=", self.value(op.obj), self.type(op.obj.type)(0.0)
                        )
                    case _:
                        raise Exception("unknown convertion type")
            case FloatType(size=to_size):
                match op.obj.type:
                    case IntType(signed=True):
                        return self.builder.sitofp(
                            self.value(op.obj), self.type(op.type)
                        )  # type: ignore
                    case IntType(signed=False):
                        return self.builder.uitofp(
                            self.value(op.obj), self.type(op.type)
                        )  # type: ignore
                    case FloatType(size=size):
                        if to_size > size:
                            return self.builder.fpext(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                        else:
                            return self.builder.fptrunc(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                    case _:
                        raise Exception("unknown conversion type")
            case IntType(size=to_size, signed=True):
                match op.obj.type:
                    case BoolType():
                        return self.builder.sext(self.value(op.obj), self.type(op.type))  # type: ignore
                    case IntType(size=size, signed=True):
                        if to_size > size:
                            return self.builder.sext(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                        else:
                            return self.builder.trunc(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                    case IntType(size=size, signed=False):
                        if to_size > size:
                            return self.builder.zext(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                        elif to_size == size:
                            return self.value(op.obj)
                        else:
                            return self.builder.trunc(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                    case FloatType():
                        return self.builder.fptosi(
                            self.value(op.obj), self.type(op.type)
                        )  # type: ignore
                    case PointerType():
                        return self.builder.ptrtoint(
                            self.value(op.obj), self.type(op.type)
                        )  # type: ignore
                    case _:
                        raise Exception("unknown conversion type")
            case IntType(size=to_size, signed=False):
                match op.obj.type:
                    case BoolType():
                        return self.builder.zext(self.value(op.obj), self.type(op.type))  # type: ignore
                    case IntType(size=size, signed=True):
                        if to_size > size:
                            return self.builder.sext(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                        elif to_size == size:
                            return self.value(op.obj)
                        else:
                            return self.builder.trunc(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                    case IntType(size=size, signed=False):
                        if to_size > size:
                            return self.builder.zext(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                        else:
                            return self.builder.trunc(
                                self.value(op.obj), self.type(op.type)
                            )  # type: ignore
                    case FloatType():
                        return self.builder.fptoui(
                            self.value(op.obj), self.type(op.type)
                        )  # type: ignore
                    case PointerType():
                        return self.builder.ptrtoint(
                            self.value(op.obj), self.type(op.type)
                        )  # type: ignore
                    case _:
                        raise Exception("unknown conversion type")
            case PointerType():
                match op.obj.type:
                    case IntType(size=64):
                        return self.builder.inttoptr(
                            self.value(op.obj), self.type(op.type)
                        ) # type: ignore
                    case ArrayType():
                        assert op.obj.type.type == op.type.type
                        return self.value(op.obj)
                    case _:
                        raise Exception(f"Unknown conversion type {op.type}")
            case _:
                raise Exception(f"Unknown conversion type {op.type}")

    def binary(self, op: Binary) -> llir.Value:
        match op.type:
            case IntType(signed=signed):
                match op.op:
                    case BinaryOp.ADD:
                        return self.builder.add(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.SUB:
                        return self.builder.sub(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.MUL:
                        return self.builder.mul(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.DIV if signed:
                        return self.builder.sdiv(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.DIV if not signed:
                        return self.builder.udiv(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.MOD if signed:
                        return self.builder.srem(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.MOD if not signed:
                        return self.builder.urem(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.LSHIFT:
                        return self.builder.shl(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.RSHIFT if signed:
                        return self.builder.ashr(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.RSHIFT if not signed:
                        return self.builder.lshr(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.BIT_AND:
                        return self.builder.and_(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.BIT_OR:
                        return self.builder.or_(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.BIT_XOR:
                        return self.builder.xor(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
            case FloatType():
                match op.op:
                    case BinaryOp.ADD:
                        return self.builder.fadd(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.SUB:
                        return self.builder.fsub(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.MUL:
                        return self.builder.fmul(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.DIV:
                        return self.builder.fdiv(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
                    case BinaryOp.MOD:
                        return self.builder.frem(
                            self.value(op.left), self.value(op.right)
                        )  # type: ignore
        raise Exception("unknown binary op")

    def unary(self, op: Unary) -> llir.Value:
        match op.type:
            case IntType():
                match op.op:
                    case UnaryOp.NEG:
                        return self.builder.neg(self.value(op.value))  # type: ignore
                    case UnaryOp.BIT_NOT:
                        return self.builder.not_(self.value(op.value))  # type: ignore
            case BoolType():
                match op.op:
                    case UnaryOp.NOT:
                        return self.builder.icmp_signed(
                            "==", self.value(op.value), self.type(op.type)(0)
                        )
            case FloatType():
                match op.op:
                    case UnaryOp.NEG:
                        return self.builder.fneg(self.value(op.value))  # type: ignore
        raise Exception("unknown unary op")

    def comparison(self, op: Comparison) -> llir.Value:
        assert op.left.type == op.right.type
        cmpop = {
            ComparisonOp.EQUAL: "==",
            ComparisonOp.NOT_EQUAL: "!=",
            ComparisonOp.GREATER: ">",
            ComparisonOp.GREATER_EQUAL: ">=",
            ComparisonOp.LESS: "<",
            ComparisonOp.LESS_EQUAL: "<=",
        }[op.op]

        match op.left.type:
            case BoolType() | IntType(signed=True):
                return self.builder.icmp_signed(
                    cmpop, self.value(op.left), self.value(op.right)
                )
            case IntType(signed=False):
                return self.builder.icmp_unsigned(
                    cmpop, self.value(op.left), self.value(op.right)
                )
            case FloatType():
                return self.builder.fcmp_unordered(
                    cmpop, self.value(op.left), self.value(op.right)
                )
        raise Exception("unknown comparison op")

    def array(self, op: Array) -> llir.Value:
        ll_value = self.type(op.type)(llir.Undefined)
        for i, item in enumerate(op.items):
            ll_value = self.builder.insert_value(ll_value, self.value(item), i)
        return ll_value

    def create_struct(self, op: CreateStruct) -> llir.Value:
        ll_value = self.type(op.type)(llir.Undefined)
        for i, field in enumerate(op.fields):
            ll_value = self.builder.insert_value(ll_value, self.value(field), i)
        return ll_value

    def op(self, op: Op) -> llir.Value:
        raise Exception("unknown op")


def generate_llvm_ir(funcs: list[FuncIR]) -> llir.Module:
    module = llir.Module()
    generator = LLVMGenerator(module)
    for func in funcs:
        generator.create(func)
    for func in funcs:
        generator.generate(func)
    return module
