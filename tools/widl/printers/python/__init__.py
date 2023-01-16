from typing import TextIO, Optional
from dataclasses import dataclass
from ...model import *
from ..tmplt import TemplateContext


@dataclass
class Simple:
    py: str
    alias: Optional[str]
    proxy: bool
    in_arg: bool


def simple_type(
    py: str, alias: Optional[str] = None, proxy: bool = False, in_arg: bool = False
):
    return Simple(py, alias=alias, proxy=proxy, in_arg=in_arg)


simple_types = {
    "void": simple_type("None", alias="void"),
    "timestamp": simple_type("int", alias="date::sys_seconds"),
    "int": simple_type("int"),
    "unsigned": simple_type("int", alias="unsigned"),
    "short": simple_type("int", alias="short"),
    "long long": simple_type("int", alias="long long"),
    "char": simple_type("int", alias="char"),
    "int32_t": simple_type("int", alias="int32_t"),
    "int64_t": simple_type("int", alias="int64_t"),
    "uint32_t": simple_type("int", alias="uint32_t"),
    "uint64_t": simple_type("int", alias="uint64_t"),
    "bool": simple_type("bool"),
    "string": simple_type("str", alias="string_type", in_arg=True),
    "string_view": simple_type("str", alias="string_type", proxy=True, in_arg=True),
    "ascii": simple_type("str", alias="std::string", in_arg=True),
}


@dataclass
class EnumInfo:
    name: str
    items: list[str]

    @property
    def NAME(self):
        return self.name.upper()


@dataclass
class AttributeInfo:
    name: str
    type: str
    cpp_type: str
    ref: bool
    ext_attrs: dict
    last: bool = False


@dataclass
class ArgumentInfo:
    name: str
    type: str
    alias: str
    this: bool
    last: bool = False


@dataclass
class OperationInfo:
    name: str
    type: str
    alias: str
    staticmethod: bool
    classmethod: bool
    property: bool
    arguments: list[ArgumentInfo]
    external: bool
    needs_proxy: bool
    ext_attrs: dict


@dataclass
class InterfaceInfo:
    name: str
    attributes: list[AttributeInfo]
    operations: list[OperationInfo]
    is_vector: bool
    is_translatable: bool
    synthetic: bool

    @property
    def has_constructor(self):
        return not self.synthetic and len(self.attributes) < 15


class CodeContext(TemplateContext):
    def __init__(self, version: int):
        super().__init__(None, version)
        self.typing: str = ""
        self.enums: list[EnumInfo] = []
        self.interfaces: list[InterfaceInfo] = []

    def gather_from(self, objects: list[WidlClass]):
        info = PythonInterface(_gather_projext_type(objects))
        VisitAllTypesWithEnums(info).visit_all(objects)
        Visitor(info, self).visit_all(objects)


class PythonInterface(TypeVisitor):
    def __init__(self, project_types: dict[str, bool]):
        self.project_types = project_types
        self.typing: set[str] = set()
        self.vectors: set[str] = set()
        self.translatables: set[str] = set()
        self.in_vector = 0
        self.in_translatable = 0

    def on_static_var(self):
        self.typing.add("ClassVar")

    def on_optional(self, obj: WidlOptional):
        self.typing.add("Optional")
        self.on_subtype(obj)

    def on_sequence(self, obj: WidlSequence):
        self.typing.add("List")
        self.in_vector += 1
        self.on_subtype(obj)
        self.in_vector -= 1

    def on_translatable(self, obj: WidlTranslatable):
        self.in_translatable += 1
        self.on_subtype(obj)
        self.in_translatable -= 1

    def on_simple(self, obj: WidlSimple):
        type_name = (
            obj.text if obj.text in self.project_types else simple_types[obj.text].py
        )
        if self.in_vector:
            self.vectors.add(type_name)
        if self.in_translatable:
            self.translatables.add(type_name)


class VisitAllTypesWithEnums(VisitAllTypes):
    def on_enum(self, _: WidlEnum):
        self.visitor.on_static_var()


def arg_(name: str, type: str = "", alias: str = "", this: bool = False):
    return ArgumentInfo(name=name, type=type, alias=alias, this=this)


def method_def_(
    name: str,
    type: str = "",
    alias: str = "",
    arguments: list[ArgumentInfo] = [],
    staticmethod=False,
    classmethod=False,
    property=False,
    external=False,
    needs_proxy=False,
    ext_attrs={},
):
    if len(arguments):
        arguments[-1].last = True
    return OperationInfo(
        name=name,
        type=type,
        alias=alias,
        staticmethod=staticmethod,
        classmethod=classmethod,
        property=property,
        arguments=arguments,
        external=external,
        needs_proxy=needs_proxy,
        ext_attrs=ext_attrs,
    )


def classmethod_(
    name: str,
    type: str = "",
    alias: str = "",
    arguments: list[ArgumentInfo] = [],
    external=False,
    needs_proxy=False,
    ext_attrs={},
):
    return method_def_(
        name=name,
        type=type,
        alias=alias,
        classmethod=True,
        arguments=[arg_("cls", this=True), *arguments],
        external=external,
        needs_proxy=needs_proxy,
        ext_attrs=ext_attrs,
    )


def staticmethod_(
    name: str,
    type: str = "",
    alias: str = "",
    arguments: list[ArgumentInfo] = [],
    external=False,
    needs_proxy=False,
    ext_attrs={},
):
    return method_def_(
        name=name,
        type=type,
        alias=alias,
        staticmethod=True,
        arguments=arguments,
        external=external,
        needs_proxy=needs_proxy,
        ext_attrs=ext_attrs,
    )


def method_(
    name: str,
    type: str = "",
    alias: str = "",
    arguments: list[ArgumentInfo] = [],
    external=False,
    needs_proxy=False,
    ext_attrs={},
):
    return method_def_(
        name=name,
        type=type,
        alias=alias,
        arguments=[arg_("self", this=True), *arguments],
        external=external,
        needs_proxy=needs_proxy,
        ext_attrs=ext_attrs,
    )


def property_(
    name: str, type: str = "", arguments: list[ArgumentInfo] = [], external=False
):
    return method_def_(
        name=name,
        type=type,
        property=True,
        arguments=[arg_("self"), *arguments],
        external=external,
    )


def class_(
    name: str,
    attributes: list[AttributeInfo] = [],
    operations: list[OperationInfo] = [],
    is_translatable=False,
    is_vector=False,
    synthetic=True,
):
    return InterfaceInfo(
        name,
        attributes=attributes,
        operations=operations,
        is_vector=is_vector,
        is_translatable=is_translatable,
        synthetic=synthetic,
    )


class Visitor(ClassVisitor):
    def __init__(self, info: PythonInterface, ctx: CodeContext):
        super(ClassVisitor).__init__()
        self.ctx = ctx
        self.info = info
        self.ctx.typing = ", ".join(sorted(info.typing))
        self.add_translatable("str")

    def on_enum(self, obj: WidlEnum):
        self.ctx.enums.append(EnumInfo(name=obj.name, items=obj.items))

    def on_interface(self, obj: WidlInterface):
        attributes: list[AttributeInfo] = []
        operations: list[OperationInfo] = []
        for prop in obj.props:
            type_name = _py_type(prop.type, self.info.project_types)
            alias, proxy, in_arg = _arg_type(prop.type, self.info.project_types)
            attributes.append(
                AttributeInfo(
                    name=prop.name,
                    type=type_name,
                    cpp_type=alias,
                    ref=in_arg,
                    ext_attrs=prop.ext_attrs,
                )
            )
        if len(attributes):
            attributes[-1].last = True
        for op in obj.ops:
            external = op.ext_attrs["external"]
            needs_proxy = False
            arguments: list[ArgumentInfo] = []
            for arg in op.args:
                alias, proxy, in_arg = _arg_type(arg.type, self.info.project_types)
                needs_proxy = needs_proxy or proxy
                if in_arg:
                    alias = f"{alias} const&"
                arguments.append(
                    arg_(arg.name, _py_type(arg.type, self.info.project_types), alias)
                )
            if len(arguments):
                arguments[-1].last = True
            staticmethod = op.ext_attrs["static"]
            ctor_ = staticmethod_ if staticmethod else method_
            alias = _arg_type(op.type, self.info.project_types)[0]
            operations.append(
                ctor_(
                    op.name,
                    type=_py_type(op.type, self.info.project_types),
                    alias=alias,
                    arguments=arguments,
                    external=external or needs_proxy,
                    needs_proxy=needs_proxy,
                    ext_attrs=op.ext_attrs,
                )
            )
        is_translatable = obj.name in self.info.translatables
        self.ctx.interfaces.append(
            InterfaceInfo(
                name=obj.name,
                attributes=attributes,
                operations=operations,
                is_vector=obj.name in self.info.vectors,
                is_translatable=is_translatable,
                synthetic=False,
            )
        )

        if is_translatable:
            self.add_translatable(obj.name)

    def add_translatable(self, name):
        self.map_indexing_suite(f"translatable_{name}_items", "str", name)
        self.ctx.interfaces.append(
            class_(
                f"translatable_{name}",
                attributes=[
                    AttributeInfo(
                        "items",
                        type=f"translatable_{name}_items",
                        cpp_type="",
                        ref=False,
                        ext_attrs={},
                    )
                ],
                operations=[
                    method_("find", type=name, arguments=[arg_("index", "str")]),
                    method_(
                        "update",
                        type="bool",
                        arguments=[arg_("index", "str"), arg_("value", name)],
                    ),
                ],
            )
        )

    def map_indexing_suite(self, name: str, key: str, value: str):
        self.ctx.interfaces.append(
            class_(
                f"map_indexing_suite_{name}_entry",
                operations=[
                    method_("data", type=value),
                    method_("key", type=key),
                ],
            )
        )
        self.ctx.interfaces.append(
            class_(
                name,
                operations=[
                    method_(
                        "__contains__", type="bool", arguments=[arg_("index", type=key)]
                    ),
                    method_(
                        "__delitem__", type="None", arguments=[arg_("index", type=key)]
                    ),
                    method_(
                        "__getitem__", type=value, arguments=[arg_("index", type=key)]
                    ),
                    method_(
                        "__setitem__",
                        type="None",
                        arguments=[arg_("index", type=key), arg_("object", type=value)],
                    ),
                    method_("__iter__"),
                    method_("__len__", type="int"),
                ],
            )
        )


@dataclass
class GatherTypes(ClassVisitor):
    def __init__(self):
        self.project_names: dict[str, bool] = {}

    def on_enum(self, obj: WidlEnum):
        self.project_names[obj.name] = False

    def on_interface(self, obj: WidlInterface):
        self.project_names[obj.name] = True


def _gather_projext_type(objects: list[WidlClass]):
    return GatherTypes().visit_all(objects).project_names


@dataclass
class PythonTypes(TypeVisitor):
    project_names: dict[str, bool]

    def on_optional(self, obj: WidlOptional):
        sub = self.on_subtype(obj)
        return f"Optional[{sub}]"

    def on_sequence(self, obj: WidlSequence):
        sub = self.on_subtype(obj)
        return f"List[{sub}]"

    def on_translatable(self, obj: WidlTranslatable):
        sub = self.on_subtype(obj)
        return f"translatable_{sub}"

    def on_simple(self, obj: WidlSimple):
        if obj.text in self.project_names:
            return obj.text
        return simple_types[obj.text].py


def _py_type(widl: WidlType, project_names: dict[str, bool]):
    return widl.on_type_visitor(PythonTypes(project_names))


@dataclass
class CxxArgTypes(TypeVisitor):
    project_names: dict[str, bool]

    def on_optional(self, obj: WidlOptional):
        sub, proxy, _ = self.on_subtype(obj)
        return f"std::optional<{sub}>", proxy, True

    def on_sequence(self, obj: WidlSequence):
        sub, proxy, _ = self.on_subtype(obj)
        return f"std::vector<{sub}>", proxy, True

    def on_translatable(self, obj: WidlTranslatable):
        sub, proxy, _ = self.on_subtype(obj)
        return f"translatable<{sub}>", proxy, True

    def on_simple(self, obj: WidlSimple):
        if obj.text in self.project_names:
            return obj.text, False, self.project_names[obj.text]
        simple = simple_types[obj.text]
        return (
            simple.alias if simple.alias is not None else simple.py,
            simple.proxy,
            simple.in_arg,
        )


def _arg_type(widl: WidlType, project_names: dict[str, bool]):
    return widl.on_type_visitor(CxxArgTypes(project_names))
