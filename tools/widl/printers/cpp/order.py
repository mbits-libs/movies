from .common import simple_types, builtin_types
from ...model import *
from dataclasses import dataclass, field
from typing import List, Set, Dict

from pprint import pprint


@dataclass
class type_ref:
    name: str
    uses: Set[str] = field(default_factory=set)
    parents: Set[str] = field(default_factory=set)
    depth: int = -1
    visited: bool = False

    def new_use(self, type_name: str):
        self.uses.add(type_name)

    def new_parent(self, type_name: str):
        self.parents.add(type_name)

    def get_depth(self, system: "type_system"):
        if self.depth >= 0:
            return self.depth
        if self.visited:
            return 0

        self.visited = True
        if not len(self.uses):
            self.depth = 0
        else:
            self.depth = 1 + max(
                system.types[sub].get_depth(system) for sub in self.uses
            )

        return self.depth


@dataclass
class type_system:
    types: Dict[str, type_ref] = field(default_factory=dict)
    result: List[str] = field(default_factory=list)

    def reset(self):
        for key in self.types:
            self.types[key].visited = False

    def add(self, type_name: str):
        if type_name not in self.types:
            self.types[type_name] = type_ref(type_name)
        return self.types[type_name]

    def register(self, uses: str, used: str):
        if uses == "callback" or used == "callback":
            return
        if uses == used:
            self.add(uses)
            return
        self.add(uses).new_use(used)
        self.add(used).new_parent(uses)

    def freed(self, ref: type_ref):
        for parent_name in ref.parents:
            parent = self.types[parent_name]
            if not parent.visited:
                return False

        return True

    def order(self):
        self.reset()
        for key in self.types:
            self.types[key].get_depth(self)
        self.reset()

        self.result = []
        for key in sorted(self.types.keys()):
            ref = self.types[key]
            if ref.visited or not self.freed(ref):
                continue
            self.visit(ref)

        for key in sorted(self.types.keys()):
            ref = self.types[key]
            if not ref.visited:
                self.result.append(ref.name)

        self.result.reverse()
        return self.result[:]

    def visit(self, ref: type_ref):
        stack = [ref]
        while len(stack):
            current = stack[0]
            stack = stack[1:]
            if current.visited:
                continue
            current.visited = True
            self.result.append(current.name)

            for name in sorted(
                (self.types[name].depth, index, name)
                for index, name in enumerate(reversed(sorted(current.uses)))
            ):
                sub = name[-1]
                sub_ref = self.types[sub]
                if sub_ref.visited:
                    continue
                if not self.freed(sub_ref):
                    continue
                # stack.append(sub_ref)
                self.visit(sub_ref)


class ClassDependencies(TypeVisitor, ClassVisitor):
    def __init__(self):
        super(TypeVisitor).__init__()
        super(ClassVisitor).__init__()
        self.current: str = ""
        self.types: Dict[str, WidlClass] = {}
        self.system = type_system()

    def declaration_order(self):
        order = self.system.order()
        return [self.types[key] for key in order]

    def on_simple(self, obj: WidlSimple):
        try:
            simple_types[obj.text]
            return
        except:
            if obj.text in builtin_types:
                return
            self.system.register(self.current, obj.text)

    def on_enum(self, obj: WidlEnum):
        self.current = obj.name
        self.types[self.current] = obj
        self.system.add(self.current)

    def on_interface(self, obj: WidlInterface):
        self.current = obj.name
        self.types[self.current] = obj
        self.system.add(self.current)
        if obj.inheritance is not None:
            self.system.register(self.current, obj.inheritance)

        for prop in obj.props:
            prop.type.on_type_visitor(self)
        for op in obj.ops:
            op.type.on_type_visitor(self)
            for arg in op.args:
                arg.type.on_type_visitor(self)


def reorder_types(objects: list[WidlClass]):
    deps = ClassDependencies()
    deps.visit_all(objects)
    objects[:] = deps.declaration_order()
