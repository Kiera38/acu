from __future__ import annotations

from dataclasses import dataclass
from contextlib import contextmanager

from acu.semanal import ir, types
from acu.source import Source, Location
from acu.errors import CompilationError, Note


@dataclass
class Scope:
    vars: dict[str, ir.VarDecl | ir.Arg]
    funcs: dict[str, ir.Func]
    structs: dict[str, types.Struct]
    is_loop: bool
    is_function: bool


@dataclass
class UsedPackage:
    context: Context
    name: list[str]

    def find(self, name: str, location: Location):
        return self.context.find_in_package(self.name, name, location)


class Context:
    def __init__(self, source: Source) -> None:
        self.source = source
        self.scopes: list[Scope] = []
        self.push_scope()
        self.blocks: list[ir.Block] = []
        # Импорты: для "using module" -> {module_name: module_context}
        #          для "from module using names" -> {local_name: item}
        self.qualified_imports: dict[tuple[str, ...], Context] = {}
        # используемые, но не импортированные пакеты (для поиска элементов по типу pkg.module.func())
        self.used_packages: set[tuple[str, ...]] = set()
        self.unqualified_imports: dict[str, ir.Func | types.Struct] = {}

    def add_qualified_import(
        self, module_name: list[str], module_context: Context
    ) -> None:
        """Добавить qualified import (using module_name)"""
        name = tuple(module_name)
        for i in range(1, len(module_name)):
            self.used_packages.add(name[:i])
        self.qualified_imports[name] = module_context

    def add_unqualified_import(
        self, local_name: str, item: ir.Func | types.Struct
    ) -> None:
        """Добавить unqualified import (from module using name)"""
        self.unqualified_imports[local_name] = item

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

    def find(
        self, name: str, location: Location
    ) -> ir.VarDecl | ir.Arg | ir.Func | types.Struct | Context | UsedPackage:
        # Сначала проверяем локальные переменные, функции и структуры
        for scope in reversed(self.scopes):
            if var := scope.vars.get(name):
                return var
            if func := scope.funcs.get(name):
                return func
            if struct := scope.structs.get(name):
                return struct

        # Проверяем unqualified imports (from module using name)
        if item := self.unqualified_imports.get(name):
            return item

        if (name,) in self.qualified_imports:
            return self.qualified_imports[(name,)]
        
        if (name,) in self.used_packages:
            return UsedPackage(self, [name])

        raise CompilationError(
            location,
            f"name '{name}' not found",
            self.source,
            helps=[
                Note(
                    f"Check that '{name}' is defined before use, or that it's spelled correctly"
                )
            ],
        )
    
    def find_in_module(self, context: Context, name: str, location: Location):
        """Ищет в context, затем ищет подходящие импортированнные пакеты и модули"""
        try:
            return context.find(name, location)
        except CompilationError:
            module_name = (*context.source.name.split('.'), name)
            if mod_context := self.qualified_imports.get(module_name):
                return mod_context
            
            if module_name in self.used_packages:
                return UsedPackage(self, list(module_name))

            raise
    
    def find_in_package(self, package_name: list[str], name: str, location: Location):
        module_name = (*package_name, name)
        if context := self.qualified_imports.get(module_name):
            return context
        
        if module_name in self.used_packages:
            return UsedPackage(self, list(module_name))
        
        raise CompilationError(
            location,
            f"name '{name}' is not imported from '{package_name}'",
            self.source,
            helps=[
                Note(
                    f"Check that '{name}' is defined before use, or that it's spelled correctly"
                )
            ],
        )

    def add_var(self, var: ir.VarDecl | ir.Arg) -> None:
        self.scopes[-1].vars[var.name] = var

    def add_struct(self, struct: types.Struct) -> None:
        self.scopes[-1].structs[struct.name] = struct

    def get_struct(self, name: str) -> types.Struct:
        for scope in reversed(self.scopes):
            if name in scope.structs:
                return scope.structs[name]
        raise ValueError(f"Struct '{name}' not found in any scope")

    def add_func(self, func: ir.Func) -> None:
        self.scopes[-1].funcs[func.name] = func

    def get_func(self, name: str) -> ir.Func:
        for scope in reversed(self.scopes):
            if name in scope.funcs:
                return scope.funcs[name]
        raise ValueError(f"Function '{name}' not found in any scope")

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


def create_context(module: ir.Module, source: Source):
    context = Context(source)
    for func in module.funcs:
        context.add_func(func)
    for struct in module.structs:
        context.add_struct(struct)
    return context
