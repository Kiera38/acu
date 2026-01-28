from typing import List

from acu.errors import CompilationError, Note
from acu.source import Source

from acu.refanal.dataflow import analyze_must_defined_regs, get_cfg
from acu.refanal.flow_graph_ir import (
    BasicBlock,
    ControlOp,
    FuncIR,
    GetAddress,
    Register,
    Specifier,
    Store,
    Value,
)


def all_values(args: list[Register], blocks: list[BasicBlock]) -> list[Value]:
    """Return the set of all values that may be initialized in the blocks.

    This omits registers that are only read.
    """
    values: list[Value] = list(args)
    seen_registers = set(args)

    for block in blocks:
        for op in block.ops:
            if not isinstance(op, ControlOp):
                if isinstance(op, Store) and isinstance(op.dest, Register):
                    if op.dest not in seen_registers:
                        values.append(op.dest)
                        seen_registers.add(op.dest)
                elif op.is_void:
                    continue
                else:
                    # If we take the address of a register, it might get initialized.
                    if (
                        isinstance(op, GetAddress)
                        and isinstance(op.src, Register)
                        and op.src not in seen_registers
                    ):
                        values.append(op.src)
                        seen_registers.add(op.src)
                    values.append(op)

    return values


def all_values_full(args: list[Register], blocks: list[BasicBlock]) -> list[Value]:
    """Return set of all values that are initialized or accessed."""
    values: list[Value] = list(args)
    seen_registers = set(args)

    for block in blocks:
        for op in block.ops:
            for source in op.sources():
                # Look for uninitialized registers that are accessed. Ignore
                # non-registers since we don't allow ops outside basic blocks.
                if isinstance(source, Register) and source not in seen_registers:
                    values.append(source)
                    seen_registers.add(source)
            if not isinstance(op, ControlOp):
                if isinstance(op, Store) and isinstance(op.dest, Register):
                    if op.dest not in seen_registers:
                        values.append(op.dest)
                        seen_registers.add(op.dest)
                elif op.is_void:
                    continue
                else:
                    values.append(op)

    return values


class RefAnalysisError(Exception):
    pass


def analyze(fn: FuncIR, source: Source) -> None:
    """Perform reference analysis for fn.

    Checks performed:
    - set `specifier` on values: function args -> LET, registers -> VAR,
      other produced values (ops, literals) -> VAL
    - detect use of possibly-uninitialized registers (report error)
    - detect address escapes: if `GetAddress(src)` value is live at a
      program point where `src` is not live, report error (reference outlives value)
    """
    cfg = get_cfg(fn.blocks)
    initial_defined: set[Value] = set(fn.args)
    maybe_def = analyze_must_defined_regs(
        fn.blocks, cfg, initial_defined, all_values(fn.args, fn.blocks)
    )
    # live = analyze_live_regs(fn.blocks, cfg)

    errors: List[str] = []

    for b in fn.blocks:
        for idx, op in enumerate(b.ops):
            before = maybe_def.before.get((b, idx), set())
            # Disallow assigning to LET (immutable) registers that are already initialized.
            if isinstance(op, Store) and isinstance(op.dest, Register):
                if op.dest.specifier == Specifier.LET and op.dest in before:
                    loc = getattr(op, "location", None) or getattr(
                        op.dest, "location", None
                    )
                    raise CompilationError(
                        loc,
                        f"Assignment to already-initialized immutable 'let' variable '{op.dest.name}'",
                        source,
                        helps=[Note("Variables declared with 'let' cannot be reassigned")]
                    )

            # For every source that is a Register (non-trivial), ensure it is
            # possibly defined at this program point
            for src in op.sources():
                if isinstance(src, Register):
                    if src not in before:
                        loc = getattr(op, "location", None) or getattr(
                            src, "location", None
                        )
                        raise CompilationError(
                            loc,
                            f"Use of possibly uninitialized variable '{src.name}'",
                            source,
                            helps=[Note(f"Make sure '{src.name}' is assigned a value before being used")]
                        )
