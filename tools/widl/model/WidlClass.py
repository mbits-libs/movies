from .WidlExtAttribute import (
    WidlExtendable,
    WidlExtAttribute,
    enum_ext_attrs,
    interface_ext_attrs,
    attribute_ext_attrs,
    oparg_ext_attrs,
    operation_ext_attrs,
)
from ..types import file_pos
from typing import Callable


class ClassVisitor:
    def __init__(self):
        pass

    def on_class(self, _: "WidlClass"):
        pass

    def on_enum(self, obj: "WidlEnum"):
        return self.on_class(obj)

    def on_interface(self, obj: "WidlInterface"):
        return self.on_class(obj)

    def visit_all(self, objects: list["WidlClass"]):
        for object in objects:
            object.on_class_visitor(self)
        self.all_visited()
        return self

    def visit_one(self, object: "WidlClass"):
        object.on_class_visitor(self)
        return self

    def all_visited(self):
        pass


def visit_all_classes(objects: list["WidlClass"], visitors: list[ClassVisitor]):
    for visitor in visitors:
        visitor.visit_all(objects)


class TypeVisitor:
    def __init__(self):
        pass

    def on_type(self, _: "WidlType"):
        pass

    def on_simple(self, obj: "WidlSimple"):
        return self.on_type(obj)

    def on_subtype(self, obj: "WidlComplex"):
        return obj.sub.on_type_visitor(self)

    def on_complex(self, obj: "WidlComplex"):
        return self.on_subtype(obj)

    def on_sequence(self, obj: "WidlSequence"):
        return self.on_complex(obj)

    def on_translatable(self, obj: "WidlTranslatable"):
        return self.on_complex(obj)

    def on_optional(self, obj: "WidlOptional"):
        return self.on_complex(obj)


class WidlClass(WidlExtendable):
    def __init__(
        self,
        type: str,
        name: str,
        ext_attrs: list[WidlExtAttribute],
        transform: Callable[[list[WidlExtAttribute]], dict],
        pos: file_pos,
    ):
        super().__init__(ext_attrs, transform, pos)
        self.type = type
        self.name = name
        self.partial = False

    def on_class_visitor(self, _: ClassVisitor):
        pass


class WidlEnum(WidlClass):
    def __init__(
        self,
        name: str,
        items: list[str],
        ext_attrs: list[WidlExtAttribute],
        pos: file_pos,
    ):
        super().__init__("enum", name, ext_attrs, enum_ext_attrs, pos)
        self.items = items

    def __str__(self):
        return "enum {} ({})".format(self.name, ", ".join(self.items))

    def on_class_visitor(self, visitor: ClassVisitor):
        return visitor.on_enum(self)


class WidlType:
    def __init__(self):
        pass

    def on_type_visitor(self, _: TypeVisitor) -> any:
        pass


class WidlSimple(WidlType):
    def __init__(self, text: str):
        super().__init__()
        self.text = text

    def __str__(self):
        return self.text

    def on_type_visitor(self, visitor: TypeVisitor):
        return visitor.on_simple(self)


class WidlComplex(WidlType):
    def __init__(self, sub: WidlType):
        super().__init__()
        self.sub = sub

    def on_type_visitor(self, visitor: TypeVisitor):
        return visitor.on_complex(self)


class WidlSequence(WidlComplex):
    def __str__(self):
        return f"{self.sub}[]"

    def on_type_visitor(self, visitor: TypeVisitor):
        return visitor.on_sequence(self)


class WidlTranslatable(WidlComplex):
    def __str__(self):
        return f"{{lang: {self.sub}}}"

    def on_type_visitor(self, visitor: TypeVisitor):
        return visitor.on_translatable(self)


class WidlOptional(WidlComplex):
    def __str__(self):
        return f"{self.sub}?"

    def on_type_visitor(self, visitor: TypeVisitor):
        return visitor.on_optional(self)


class WidlAttribute(WidlExtendable):
    def __init__(
        self,
        name: str,
        type: WidlType,
        ext_attrs: list[WidlExtAttribute],
        pos: file_pos,
    ):
        super().__init__(ext_attrs, attribute_ext_attrs, pos)
        self.name = name
        self.type = type

    def __str__(self):
        return f"{self.type} {self.name};"


class WidlArgument(WidlExtendable):
    def __init__(
        self,
        name: str,
        type: WidlType,
        ext_attrs: list[WidlExtAttribute],
        pos: file_pos,
    ):
        super().__init__(ext_attrs, oparg_ext_attrs, pos)
        self.name = name
        self.type = type


class WidlOperation(WidlExtendable):
    def __init__(
        self,
        name: str,
        type: WidlType,
        args: list[WidlArgument],
        ext_attrs: list[WidlExtAttribute],
        pos: file_pos,
    ):
        super().__init__(ext_attrs, operation_ext_attrs, pos)
        self.name = name
        self.type = type
        self.args = args


class WidlInterface(WidlClass):
    def __init__(
        self,
        name: str,
        props: list[WidlAttribute],
        ops: list[WidlOperation],
        ext_attrs: list[WidlExtAttribute],
        pos: file_pos,
    ):
        super().__init__("interface", name, ext_attrs, interface_ext_attrs, pos)
        self.props = props
        self.ops = ops

    def __str__(self):
        return f"interface {self.name} (...)"

    def on_class_visitor(self, visitor: ClassVisitor):
        return visitor.on_interface(self)


class VisitAllTypes(ClassVisitor):
    def __init__(self, visitor: TypeVisitor):
        self.visitor = visitor

    def visit(self, type: WidlType):
        type.on_type_visitor(self.visitor)

    def on_interface(self, obj: WidlInterface):
        for prop in obj.props:
            self.visit(prop.type)

        for op in obj.ops:
            self.visit(op.type)

            for arg in op.args:
                self.visit(arg.type)
