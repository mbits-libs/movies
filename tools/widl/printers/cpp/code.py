from .common import *
from ...model import *
from typing import TextIO
from ..tmplt import TemplateContext
from dataclasses import dataclass


class CollectTypes(ClassVisitor):
    def __init__(self):
        self.enums: set[str] = set()
        self.merge_with: dict[str, list[tuple[str, str]]] = {}

    def on_enum(self, obj: WidlEnum):
        self.enums.add(obj.name)

    def on_interface(self, obj: WidlInterface):
        if len(obj.ext_attrs["merge_with"]):
            self.merge_with[obj.name] = obj.ext_attrs["merge_with"]


class ExtractSimpleType(TypeVisitor):
    def on_simple(self, obj: WidlSimple):
        return obj.text


def _simple(type: WidlType):
    return type.on_type_visitor(ExtractSimpleType())


@dataclass
class EnumInfo:
    name: str

    @property
    def NAME(self):
        return self.name.upper()


@dataclass
class MergeWith:
    type: str
    name: str


@dataclass
class AttributeInfo:
    name: str
    ext_attrs: dict
    merge_with: list[MergeWith]

    @property
    def op_load(self):
        return (
            "load_or_value"
            if self.ext_attrs["or_value"]
            else "load_zero"
            if self.ext_attrs["empty"] == "allow"
            else "load"
        )

    @property
    def op_store(self):
        return "store_or_value" if self.ext_attrs["or_value"] else "store"


@dataclass
class InterfaceInfo:
    name: str
    add_merge: bool
    add_serdes: bool
    ext_attrs: dict
    attributes: list[AttributeInfo]
    merge_with: list[MergeWith]

    @property
    def spcs(self):
        return " " * len(self.name)


class CodeContext(TemplateContext):
    def __init__(self, output: TextIO, version: int, header: str):
        super().__init__(output, version)
        self.header = header
        self.enums: list[EnumInfo] = []
        self.interfaces: list[InterfaceInfo] = []


class Visitor(ClassVisitor):
    def __init__(self, merge_with: dict[str, list[tuple[str, str]]], ctx: CodeContext):
        super(ClassVisitor).__init__()
        self.merge_with = merge_with
        self.ctx = ctx

    def on_interface(self, obj: WidlInterface):
        load_from, merge_mode, merge_with, load_postproc, merge_postproc = (
            obj.ext_attrs["from"],
            obj.ext_attrs["merge"],
            obj.ext_attrs["merge_with"],
            obj.ext_attrs["load_postproc"],
            obj.ext_attrs["merge_postproc"],
        )
        obj_ext_attrs = {**obj.ext_attrs}
        del (obj_ext_attrs["merge_with"],)
        obj_ext_attrs["has_to_string"] = load_from != "none"

        attributes: list[AttributeInfo] = []
        for prop in obj.props:
            simple_type = _simple(prop.type)
            params = [
                MergeWith(type_name, name)
                for type_name, name in self.merge_with.get(simple_type, [])
            ]
            attributes.append(AttributeInfo(prop.name, prop.ext_attrs, params))

        self.ctx.interfaces.append(
            InterfaceInfo(
                obj.name,
                add_merge=merge_mode == "auto",
                add_serdes=load_from == "map",
                ext_attrs=obj_ext_attrs,
                attributes=attributes,
                merge_with=[
                    MergeWith(type_name, arg_name) for type_name, arg_name in merge_with
                ],
            )
        )


def print_code(objects: list[WidlClass], output: TextIO, header: str, version: int):
    types = CollectTypes().visit_all(objects)

    ctx = CodeContext(output, version, header)
    ctx.enums = [EnumInfo(name) for name in sorted(types.enums)]
    Visitor(types.merge_with, ctx).visit_all(objects)
    ctx.emit("code.mustache")
