from .common import *
from ...model import *
from typing import TextIO
from ..tmplt import TemplateContext
from dataclasses import dataclass


class CppTypes(TypeVisitor):
    def on_optional(self, obj: WidlOptional):
        sub = self.on_subtype(obj)
        return f"std::optional<{sub}>"

    def on_sequence(self, obj: WidlSequence):
        sub = self.on_subtype(obj)
        return f"std::vector<{sub}>"

    def on_translatable(self, obj: WidlTranslatable):
        sub = self.on_subtype(obj)
        return f"translatable<{sub}>"

    def on_simple(self, obj: WidlSimple):
        try:
            return simple_types[obj.text][1]
        except KeyError:
            return obj.text


def _cpp_type(widl: WidlType):
    return widl.on_type_visitor(CppTypes())


class EnumInfo:
    def __init__(self, name: str, items: list[str], ext_attrs: dict):
        self.name = name
        self.NAME = name.upper()
        self.item = items
        self.ext_attrs = ext_attrs

    @property
    def text(self):
        lines: list[str] = [
            f"#define {self.NAME}_X(X)",
            *[f"    X({name})" for name in self.item[:-1]],
        ]
        max_len = max(len(line) for line in lines)
        text = []
        for line in lines:
            text.append(f"{line:<{max_len}} \\")
        text.append(f"    X({self.item[-1]})")
        return "\n".join(text)


class AttributeInfo:
    def __init__(
        self, name: str, type_name: str, default: str, guards: list[str], pos: file_pos
    ):
        self.name = name
        self.type = type_name
        self.default = f"{{{default}}}"
        self.guards = guards
        self.file_line = pos.line
        self.file_name = pos.path


class ArgumentInfo:
    def __init__(self, name: str, type_name: str, ext_attrs: dict):
        self.name = name
        self.type = type_name
        self.ext_attrs = ext_attrs
        self.comma = True


class OperationInfo:
    def __init__(
        self,
        name: str,
        type_name: str,
        guards: list[str],
        ext_attrs: dict,
        args: list[ArgumentInfo],
        pos: file_pos,
    ):
        self.name = name
        self.type = type_name
        self.guards = guards
        self.ext_attrs = ext_attrs
        self.args = args
        self.file_line = pos.line
        self.file_name = pos.path
        if len(self.args):
            self.args[-1].comma = False


@dataclass
class InterfaceInfo:
    name: str
    ext_attrs: dict
    attributes: list[AttributeInfo]
    operations: list[OperationInfo]
    file_line: int
    file_name: str


class HeaderContext(TemplateContext):
    def __init__(self, output: TextIO, version: int):
        super().__init__(output, version)
        self.includes: list[str] = []
        self.enums: list[EnumInfo] = []
        self.interfaces: list[InterfaceInfo] = []


class Visitor(TypeVisitor, ClassVisitor):
    def __init__(self, project_types: list[str], ctx: HeaderContext):
        super(TypeVisitor).__init__()
        super(ClassVisitor).__init__()
        self.files: set[str] = set()
        self.project_types = project_types
        self.ctx = ctx

    def all_visited(self):
        files = {*self.files, "<movies/types.hpp>"}
        self.ctx.includes = list(sorted(files))

    def on_optional(self, obj: WidlOptional):
        self.on_subtype(obj)
        self.files.add("<optional>")

    def on_sequence(self, obj: WidlSequence):
        self.on_subtype(obj)
        self.files.add("<vector>")

    def on_simple(self, obj: WidlSimple):
        try:
            info = simple_types[obj.text]
            if info[0] is not None:
                self.files.add(info[0])
        except KeyError:
            if obj.text in builtin_types or obj.text in self.project_types:
                return
            print(f"unknown type: {obj.text}")

    def on_interface(self, obj: WidlInterface):
        attributes: list[AttributeInfo] = []
        operations = self._initial_ops(obj)

        for prop in obj.props:
            prop.type.on_type_visitor(self)

            guard, guards = (prop.ext_attrs["guard"], prop.ext_attrs["guards"])
            if guard is not None:
                guards.append(guard)
            attributes.append(
                AttributeInfo(
                    prop.name,
                    _cpp_type(prop.type),
                    prop.ext_attrs["default"],
                    [*guards],
                    prop.pos,
                )
            )
        for op in obj.ops:
            args: list[ArgumentInfo] = []

            op.type.on_type_visitor(self)
            for arg in op.args:
                arg.type.on_type_visitor(self)
                args.append(ArgumentInfo(arg.name, _cpp_type(arg.type), arg.ext_attrs))

            guard, guards = (prop.ext_attrs["guard"], prop.ext_attrs["guards"])
            if guard is not None:
                guards.append(guard)
            operations.append(
                OperationInfo(
                    op.name, _cpp_type(op.type), [*guards], op.ext_attrs, args, op.pos
                )
            )

        self.ctx.interfaces.append(
            InterfaceInfo(
                obj.name,
                obj.ext_attrs,
                attributes,
                operations,
                file_line=obj.pos.line,
                file_name=obj.pos.path,
            )
        )

    def _initial_ops(self, obj: WidlInterface) -> list[OperationInfo]:
        initial: list[OperationInfo] = []

        load_from, merge_mode, merge_with, spaceship, load_postproc, merge_postproc = (
            obj.ext_attrs["from"],
            obj.ext_attrs["merge"],
            obj.ext_attrs["merge_with"],
            obj.ext_attrs["spaceship"],
            obj.ext_attrs["load_postproc"],
            obj.ext_attrs["merge_postproc"],
        )
        obj.ext_attrs["has_to_string"] = load_from != "none"
        add_merge = merge_mode != "none"
        comp_type, comp_op = ("auto", "<=>") if spaceship else ("bool", "==")

        initial.append(
            OperationInfo(
                f"operator{comp_op}",
                comp_type,
                [],
                {"defaulted": True},
                [ArgumentInfo("rhs", obj.name, {"in": True})],
                obj.pos,
            )
        )
        if load_from != "none":
            initial.extend(
                [
                    OperationInfo(
                        "to_json",
                        "json::node",
                        [],
                        {"throws": True},
                        [],
                        obj.pos,
                    ),
                    OperationInfo(
                        "from_json",
                        "json::conv_result",
                        [],
                        {"throws": True, "mutable": True},
                        [
                            ArgumentInfo("data", f"json::{load_from}", {"in": True}),
                            ArgumentInfo(
                                "dbg", "std::string", {"out": True, "in": True}
                            ),
                        ],
                        obj.pos,
                    ),
                ]
            )

        if load_postproc:
            initial.append(
                OperationInfo(
                    "load_postproc",
                    "json::conv_result",
                    [],
                    {"throws": True, "mutable": True},
                    [
                        ArgumentInfo("dbg", "std::string", {"out": True, "in": True}),
                    ],
                    obj.pos,
                )
            )

        if add_merge:
            args = [
                ArgumentInfo("new_data", obj.name, {"in": True}),
            ]
            for arg_type, arg_name in merge_with:
                args.append(ArgumentInfo(arg_name, arg_type, {}))
            initial.append(
                OperationInfo(
                    "merge",
                    "json::conv_result",
                    [],
                    {"throws": True, "mutable": True},
                    args,
                    obj.pos,
                )
            )
            if merge_postproc:
                initial.append(
                    OperationInfo(
                        "merge_postproc",
                        "json::conv_result",
                        [],
                        {"throws": True, "mutable": True},
                        [],
                        obj.pos,
                    )
                )

        return initial

    def on_enum(self, obj: WidlEnum):
        self.ctx.enums.append(EnumInfo(obj.name, obj.items, obj.ext_attrs))


def print_header(objects: list[WidlClass], output: TextIO, version: int):
    ctx = HeaderContext(output, version)
    Visitor([obj.name for obj in objects], ctx).visit_all(objects)
    ctx.emit("header.mustache")
