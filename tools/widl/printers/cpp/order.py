from .common import simple_types, builtin_types
from ...model import *


class ClassDependencies(TypeVisitor, ClassVisitor):
    def __init__(self):
        super(TypeVisitor).__init__()
        super(ClassVisitor).__init__()
        self.types: dict[str, WidlClass] = {}
        self.refs: dict[str, set[str]] = {}
        self.current: str = ""

    def declaration_order(self):
        result = []
        refs = {key: {*self.refs[key]} for key in self.refs}
        while len(refs):
            batch: list[str] = []
            for ref in refs:
                if not len(refs[ref]):
                    batch.append(ref)
            for ref in batch:
                del refs[ref]
            for ref in refs:
                for dep in batch:
                    try:
                        refs[ref].remove(dep)
                    except KeyError:
                        pass

            batch.sort()
            result.extend(batch)

            if not len(batch):
                break

        result.extend(sorted(refs.keys()))
        return [self.types[key] for key in result]

    def on_simple(self, obj: WidlSimple):
        try:
            simple_types[obj.text]
            return
        except:
            if obj.text in builtin_types:
                return
            self.refs[self.current].add(obj.text)

    def on_enum(self, obj: WidlEnum):
        self.current = obj.name
        self.refs[self.current] = set()
        self.types[self.current] = obj

    def on_interface(self, obj: WidlInterface):
        self.current = obj.name
        self.refs[self.current] = set()
        self.types[self.current] = obj
        if obj.inheritance is not None:
            self.refs[self.current].add(obj.inheritance)

        for prop in obj.props:
            prop.type.on_type_visitor(self)


def reorder_types(objects: list[WidlClass]):
    deps = ClassDependencies()
    deps.visit_all(objects)
    objects[:] = deps.declaration_order()
