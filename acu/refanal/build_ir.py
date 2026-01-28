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
    Deref,
    Float,
    FuncIR,
    GetAddress,
    GetField,
    GetItem,
    Goto,
    Integer,
    Op,
    Register,
    Return,
    SetField,
    SetItem,
    Store,
    Unary,
    Undef,
    Value,
)
from acu.semanal import ir
from acu.semanal.types import (
    Struct,
    StructType,
    Type,
    TypedFunc,
    bool_type,
)


class IRBuilder(ir.InstVisitor[Value]):
    def __init__(self, func_map: dict[str, FuncIR], func: TypedFunc):
        self.func_map = func_map
        self.func = func
        self.value_map: dict[ir.Inst, Value] = {}
        self.blocks: list[BasicBlock] = []
        self.current: BasicBlock = None  # type: ignore
        self.loop_stack: list[tuple[BasicBlock, BasicBlock]] = []

    def create_args(self, func: ir.Func) -> list[Register]:
        args: list[Register] = []
        for i in range(func.arg_count):
            arg_inst = func.code.code[i]
            reg = Register(
                location=arg_inst.location,
                type=self.type(arg_inst),
                name=getattr(arg_inst, "name", ""),
                is_arg=True,
            )
            self.value_map[arg_inst] = reg
            args.append(reg)
        return args

    def process_block(self, sem_block: ir.Block):
        block = self.create_block()
        self.process_block_into(block, sem_block)
        return block

    def process_block_into(self, block_obj: BasicBlock, sem_block: ir.Block):
        self.current = block_obj
        for inst in sem_block.code:
            res = inst.accept(self)
            self.value_map[inst] = res

    def add(self, op: Op):
        self.current.ops.append(op)
        return op

    def type(self, i: ir.Inst) -> Type:
        return self.func.inst_type(i)

    def new_reg_for(self, inst: ir.Inst, name: str = "") -> Register:
        reg = Register(location=inst.location, type=self.type(inst), name=name)
        return reg

    def value(self, inst: ir.Inst) -> Value:
        return self.value_map[inst]

    def create_block(self) -> BasicBlock:
        block = BasicBlock(label=len(self.blocks))
        self.blocks.append(block)
        return block

    def promote_type(self, t1: Type, t2: Type) -> Type:
        if t1 == t2:
            return t1
        if t1.can_convert(t2):
            return t2
        if t2.can_convert(t1):
            return t1
        return t1

    def ensure_type(self, val: Value, to_type: Type, location):
        if val.type == to_type:
            return val

        cast = Cast(location=location, type=to_type, obj=val)
        return self.add(cast)

    def literal(self, inst: ir.Literal):
        v = inst.value
        if isinstance(v, bool):
            val = Boolean(location=inst.location, value=v)
        if isinstance(v, int):
            val = Integer(location=inst.location, value=v)
        elif isinstance(v, float):
            val = Float(location=inst.location, value=v)
        else:
            val = Undef(type=self.type(inst))
        return val

    def var(self, inst: ir.VarDecl):
        reg = Register(location=inst.location, type=self.type(inst), name=inst.name)
        return reg

    def arg(self, inst: ir.Arg):
        return self.value_map[inst]

    def binary(self, inst: ir.Binary):
        left = self.value(inst.left)
        right = self.value(inst.right)
        # Promote operand types to the binary result type if needed
        result_type = self.type(inst)
        left = self.ensure_type(left, result_type, inst.location)
        right = self.ensure_type(right, result_type, inst.location)
        op = Binary(
            location=inst.location,
            left=left,
            right=right,
            op=inst.op,
        )
        return self.add(op)

    def comparison(self, inst: ir.Comparison):
        if not inst.comparators:
            return Undef(type=self.type(inst))

        res_reg = self.new_reg_for(inst, name="comparison")
        join_b = self.create_block()

        # Process first comparison
        comp = inst.comparators[0]
        self.process_block_into(self.current, comp.value)
        rhs_inst = comp.value.code[-1] if comp.value.code else None
        if rhs_inst is None:
            return Undef(type=self.type(inst))

        left = self.value(inst.left)
        right = self.value(rhs_inst)
        # Promote operand types to a common type before comparison
        common = self.promote_type(left.type, right.type)
        left = self.ensure_type(left, common, inst.location)
        right = self.ensure_type(right, common, inst.location)
        first_result = Comparison(
            location=inst.location,
            left=left,
            right=right,
            op=comp.op,
        )
        self.add(first_result)
        result = first_result

        # Process rest of comparators with short-circuit
        for i in range(1, len(inst.comparators)):
            curr_comp = inst.comparators[i]

            # Create blocks for short-circuit
            continue_b = self.create_block()
            false_b = self.create_block()
            # Branch on current result
            br = Branch(
                location=inst.location,
                value=result,
                true=continue_b,
                false=false_b,
            )
            self.add(br)

            # False branch: store false and goto join
            false_b.ops.append(
                Store(
                    location=inst.location,
                    dest=res_reg,
                    src=Boolean(location=inst.location, value=False),
                )
            )
            false_b.ops.append(Goto(location=inst.location, label=join_b))

            # Continue branch: process next comparison
            self.process_block_into(continue_b, curr_comp.value)
            curr_rhs_inst = curr_comp.value.code[-1] if curr_comp.value.code else None
            if curr_rhs_inst is None:
                return Undef(type=self.type(inst))

            # Left side is the right side of previous comparison
            new_left = self.value(rhs_inst)
            new_right = self.value(curr_rhs_inst)
            # promote to common type
            common = self.promote_type(new_left.type, new_right.type)
            new_left = self.ensure_type(new_left, common, inst.location)
            new_right = self.ensure_type(new_right, common, inst.location)

            curr_result = Comparison(
                location=inst.location,
                left=new_left,
                right=new_right,
                op=curr_comp.op,
            )
            self.add(curr_result)
            result = curr_result
            rhs_inst = curr_rhs_inst

        # Store final result
        self.current.ops.append(
            Store(
                location=inst.location,
                dest=res_reg,
                src=result,
            )
        )
        self.current.ops.append(Goto(location=inst.location, label=join_b))

        self.current = join_b
        return res_reg

    def logical(self, inst: ir.Logical):
        left_val = self.value(inst.left)
        res_reg = self.new_reg_for(inst, name="logical")

        then_b = self.create_block()
        else_b = self.create_block()
        join_b = self.create_block()

        # Ensure condition is boolean-like
        left_val = self.ensure_type(left_val, bool_type, inst.location)
        if inst.op == ir.LogicalOp.AND:
            br = Branch(
                location=inst.location,
                value=left_val,
                true=then_b,
                false=else_b,
            )
            self.add(br)
            self.process_block_into(then_b, inst.right)
            rhs_inst = inst.right.code[-1] if inst.right.code else None
            rhs_val = (
                self.value(rhs_inst)
                if rhs_inst is not None
                else Undef(type=self.type(inst))
            )
            # Ensure stored value matches result register type
            rhs_val = self.ensure_type(rhs_val, res_reg.type, inst.location)
            then_b.ops.append(
                Store(
                    location=inst.location,
                    dest=res_reg,
                    src=rhs_val,
                )
            )
            if not then_b.terminated:
                then_b.ops.append(Goto(location=inst.location, label=join_b))
            # Ensure stored value matches result register type
            else_b.ops.append(
                Store(
                    location=inst.location,
                    dest=res_reg,
                    src=self.ensure_type(left_val, res_reg.type, inst.location),
                )
            )
            else_b.ops.append(Goto(location=inst.location, label=join_b))
        else:
            br = Branch(
                location=inst.location,
                value=left_val,
                true=else_b,
                false=then_b,
            )
            self.add(br)
            self.process_block_into(then_b, inst.right)
            rhs_inst = inst.right.code[-1] if inst.right.code else None
            rhs_val = (
                self.value(rhs_inst)
                if rhs_inst is not None
                else Undef(type=self.type(inst))
            )
            # Ensure stored value matches result register type
            rhs_val = self.ensure_type(rhs_val, res_reg.type, inst.location)
            then_b.ops.append(
                Store(
                    location=inst.location,
                    dest=res_reg,
                    src=rhs_val,
                )
            )
            if not then_b.terminated:
                then_b.ops.append(Goto(location=inst.location, label=join_b))
            else_b.ops.append(
                Store(
                    location=inst.location,
                    dest=res_reg,
                    src=self.ensure_type(left_val, res_reg.type, inst.location),
                )
            )
            else_b.ops.append(Goto(location=inst.location, label=join_b))

        self.current = join_b
        return res_reg

    def unary(self, inst: ir.Unary):
        val = self.value(inst.value)
        # ensure operand matches unary result/type if conversion exists
        val = self.ensure_type(val, self.type(inst), inst.location)
        op = Unary(location=inst.location, value=val, op=inst.op)
        return self.add(op)

    def call(self, inst: ir.Call):
        fn_inst = inst.value
        assert isinstance(fn_inst, ir.Literal), "Function call target must be a literal"
        if isinstance(fn_inst.value, Struct):
            return self.add(
                CreateStruct(
                    inst.location, [self.value(a) for a in inst.args], fn_inst.value
                )
            )
        assert isinstance(fn_inst.value, ir.Func), (
            "Function call target must be a function"
        )
        fn = self.func_map.get(fn_inst.value.name)
        assert fn is not None, (
            f"Function {getattr(fn_inst, 'name', None)} not found in func_map"
        )
        args_vals = [self.value(a) for a in inst.args]
        op = Call(location=inst.location, fn=fn, args=args_vals)
        return self.add(op)

    def get_attr(self, inst: ir.GetAttr):
        obj = self.value(inst.value)
        # determine field index from the object's struct type (type analysis
        # does not set inst.field), prefer raising on missing information
        obj_type = self.type(inst.value)
        if isinstance(obj_type, StructType):
            field = obj_type.struct.fields.get(inst.name)
            if field is None:
                raise AssertionError(f"field {inst.name} not found on struct")
            idx = field.index
        else:
            # fallback: assume field 0 if information missing
            idx = 0
        op = GetField(location=inst.location, obj=obj, field=idx)
        return self.add(op)

    def set_attr(self, inst: ir.SetAttr):
        obj = self.value(inst.var)
        val = self.value(inst.value)
        # lookup field index and type from the struct type of the object
        obj_type = self.type(inst.var)
        if isinstance(obj_type, StructType):
            field = obj_type.struct.fields.get(inst.name)
            if field is None:
                raise AssertionError(f"field {inst.name} not found on struct")
            idx = field.index
            # cast value to the field type if possible
            val = self.ensure_type(val, field.type, inst.location)
        else:
            idx = 0

        op = SetField(location=inst.location, obj=obj, field=idx, value=val)
        self.current.ops.append(op)
        return Undef(type=self.type(inst))

    def get_item(self, inst: ir.GetItem):
        obj = self.value(inst.value)
        index = self.value(inst.index)
        op = GetItem(location=inst.location, obj=obj, index=index)
        return self.add(op)

    def set_item(self, inst: ir.SetItem):
        obj = self.value(inst.var)
        index = self.value(inst.index)
        val = self.value(inst.value)
        op = SetItem(
            location=inst.location,
            obj=obj,
            index=index,
            value=val,
        )
        self.current.ops.append(op)
        return Undef(type=self.type(inst))

    def address_of(self, inst: ir.AddressOf):
        src = self.value(inst.value)
        op = GetAddress(location=inst.location, src=src)
        return self.add(op)

    def deref(self, inst: ir.Deref):
        src = self.value(inst.value)
        op = Deref(location=inst.location, ptr=src)
        return self.add(op)

    def store(self, inst: ir.Store):
        dest = self.value(inst.var)
        src = self.value(inst.value)
        # cast source to destination type when possible
        src = self.ensure_type(src, dest.type, inst.location)
        op = Store(location=inst.location, dest=dest, src=src)
        self.current.ops.append(op)
        return Undef(type=self.type(inst))

    def load(self, inst: ir.Load):
        src = self.value(inst.var)
        return src

    def array(self, inst: ir.Array):
        items = [self.value(i) for i in inst.items]
        op = Array(location=inst.location, items=items)
        return self.add(op)

    def return_inst(self, inst: ir.Return):
        if inst.value is None:
            val = Undef(type=self.func.type.return_type)
        else:
            val = self.value(inst.value)
            val = self.ensure_type(val, self.func.type.return_type, inst.location)
        op = Return(location=inst.location, value=val)
        self.current.ops.append(op)
        return val  # Return the value so caller knows it's assigned

    def if_inst(self, inst: ir.If):
        cond = self.value(inst.value)
        # ensure condition is boolean-ish
        cond = self.ensure_type(cond, bool_type, inst.location)
        then_b = self.create_block()
        else_b = self.create_block()
        join_b = self.create_block()

        br = Branch(
            location=inst.location,
            value=cond,
            true=then_b,
            false=else_b,
        )
        self.add(br)

        self.process_block_into(then_b, inst.then_block)
        if not self.current.terminated:
            self.add(Goto(location=inst.location, label=join_b))

        self.process_block_into(else_b, inst.else_block)
        if not self.current.terminated:
            self.add(Goto(location=inst.location, label=join_b))

        self.current = join_b
        return Undef(type=self.type(inst))

    def loop(self, inst: ir.Loop):
        body_b = self.create_block()
        exit_b = self.create_block()

        self.add(Goto(location=inst.location, label=body_b))
        self.loop_stack.append((body_b, exit_b))
        self.process_block_into(body_b, inst.block)
        self.loop_stack.pop()
        if not self.current.terminated:
            self.add(Goto(location=inst.location, label=body_b))

        self.current = exit_b
        return Undef(type=self.type(inst))

    def break_inst(self, inst: ir.Break):
        if not self.loop_stack:
            return Undef(type=self.type(inst))
        _, exit_b = self.loop_stack[-1]
        self.add(Goto(location=inst.location, label=exit_b))
        return Undef(type=self.type(inst))

    def continue_inst(self, inst: ir.Continue):
        if not self.loop_stack:
            return Undef(type=self.type(inst))
        body_b, _ = self.loop_stack[-1]
        self.add(Goto(location=inst.location, label=body_b))
        return Undef(type=self.type(inst))

    def as_inst(self, inst: ir.AsInst):
        value = self.value(inst.value)
        # ensure value can be explicitly converted to target type
        value = self.ensure_type(value, inst.type, inst.location)
        return value

    def inst(self, inst: ir.Inst):
        if inst not in self.value_map:
            self.value_map[inst] = self.new_reg_for(
                inst, name=getattr(inst, "name", "tmp")
            )
        return self.value_map[inst]


def build_func(
    func_map: dict[str, FuncIR],
    func: TypedFunc,
    ir_func: FuncIR,
) -> FuncIR:
    builder = IRBuilder(func_map, func)
    args = builder.create_args(func.func)
    ir_func.args = args
    builder.process_block(func.func.code)
    ir_func.blocks = builder.blocks
    ir_func.return_type = (
        func.type.return_type
    )  # Set the return type from the original function
    return ir_func


def build_module(funcs: list[TypedFunc]) -> list[FuncIR]:
    func_map: dict[str, FuncIR] = {}

    # First pass: create stubs
    for func in funcs:
        stub = FuncIR(
            name=func.func.name, args=[], blocks=[], location=func.func.location, source=func.source
        )
        func_map[func.func.name] = stub

    # Second pass: build each function with resolved call targets
    for func in funcs:
        build_func(func_map, func, func_map[func.func.name])

    return list(func_map.values())
