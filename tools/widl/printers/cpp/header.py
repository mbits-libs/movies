from .common import *
from ...model import *
from typing import TextIO


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


class HeaderIncludes(TypeVisitor, ClassVisitor):
    def __init__(self, output: TextIO, project_types: list[str], version: int):
        super(TypeVisitor).__init__()
        super(ClassVisitor).__init__()
        self.files: set[str] = set()
        self.output = output
        self.project_types = project_types
        self.version = version

    def all_visited(self):
        files = {*self.files, "<movies/types.hpp>"}
        for include in sorted(files):
            print(f"#include {include}", file=self.output)
        if len(self.files):
            print(file=self.output)
        print(
            f"namespace movies::v{self.version} {{\n  static constexpr auto VERSION = {self.version}u;\n",
            file=self.output,
        )

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
        for prop in obj.props:
            prop.type.on_type_visitor(self)


class HeaderDefineEnums(ClassVisitor):
    def __init__(self, output: TextIO):
        self.output = output

    def on_enum(self, obj: WidlEnum):
        x_macro = f"{obj.name.upper()}_X"
        lines: list[str] = [
            f"#define {x_macro}(X)",
            *[f"  X({name})" for name in obj.items[:-1]],
        ]
        max_len = max(len(line) for line in lines)
        for line in lines:
            print(f"{line:<{max_len}} \\", file=self.output)
        print(
            f"""\
  X({obj.items[-1]})

  enum class {obj.name} {{
#define X_DECL_TYPE(NAME) NAME,
    {x_macro}(X_DECL_TYPE)
#undef X_DECL_TYPE
  }};

""",
            file=self.output,
        )


class HeaderForwardDeclare(ClassVisitor):
    def __init__(self, output: TextIO):
        self.output = output
        self.separate = False

    def all_visited(self):
        if self.separate:
            print(file=self.output)

    def on_interface(self, obj: WidlInterface):
        print(f"  struct {obj.name};", file=self.output)
        self.separate = True


class HeaderClasses(ClassVisitor):
    def __init__(self, output: TextIO):
        self.output = output

    def on_interface(self, obj: WidlInterface):
        ext_attrs = interface_ext_attrs(obj.ext_attrs)
        load_from, merge_mode, merge_with, spaceship, load_postproc, merge_postproc = (
            ext_attrs["from"],
            ext_attrs["merge"],
            ext_attrs["merge_with"],
            ext_attrs["spaceship"],
            ext_attrs["load_postproc"],
            ext_attrs["merge_postproc"],
        )
        add_merge = merge_mode != "none"
        comp_type, comp_op = ("auto", "<=>") if spaceship else ("bool", "==")
        print(
            f"""\
  struct {obj.name} {{""",
            file=self.output,
        )
        for prop in obj.props:
            ext_attrs = attribute_ext_attrs(prop.ext_attrs)
            guard, guards, default = (
                ext_attrs["guard"],
                ext_attrs["guards"],
                ext_attrs["default"],
            )
            if guard is not None:
                guards.append(guard)
            if len(guards):
                print(
                    "#if {}".format(" ".join(f"defined({guard})" for guard in guards)),
                    file=self.output,
                )
            print(
                "    {} {}{{{}}};".format(_cpp_type(prop.type), prop.name, default),
                file=self.output,
            )
            if len(guards):
                print("#endif", file=self.output)
            pass
        print(
            f"""
    {comp_type} operator{comp_op}({obj.name} const&) const noexcept = default;

    json::node to_json() const;
    json::conv_result from_json(json::{load_from} const& data, std::string& dbg);""",
            file=self.output,
        )
        if load_postproc:
            print(
                f"    json::conv_result load_postproc(std::string& dbg);",
                file=self.output,
            )
        if add_merge:
            print(
                "    json::conv_result merge({} const&{});".format(
                    obj.name,
                    "".join(
                        f", {arg_type} {arg_name}" for arg_type, arg_name in merge_with
                    ),
                ),
                file=self.output,
            )
            if merge_postproc:
                print(
                    f"    json::conv_result merge_postproc();",
                    file=self.output,
                )
        for operation in obj.ops:
            ext_attrs = operation_ext_attrs(operation.ext_attrs)
            cpp_type = _cpp_type(operation.type)
            guard, guards = ext_attrs["guard"], ext_attrs["guards"]
            if guard is not None:
                guards.append(guard)
            is_static = ext_attrs["static"]
            static = "static " if is_static else ""
            const = "" if is_static or ext_attrs["mutable"] else " const"
            noexcept = "" if ext_attrs["throws"] else " noexcept"
            args = []

            for arg in operation.args:
                ext_attrs = oparg_ext_attrs(arg.ext_attrs)
                arg_type = _cpp_type(arg.type)
                if ext_attrs["out"]:
                    arg_type = f"{arg_type}&"
                elif ext_attrs["in"]:
                    arg_type = f"{arg_type} const&"
                args.append(f"{arg_type} {arg.name}")
            args = ", ".join(args)
            if len(guards):
                print(
                    "#if {}".format(" ".join(f"defined({guard})" for guard in guards)),
                    file=self.output,
                )
            print(
                f"    {static}{cpp_type} {operation.name}({args}){const}{noexcept};",
                file=self.output,
            )
            if len(guards):
                print("#endif", file=self.output)
        print("  };\n", file=self.output)


def print_header(objects: list[WidlClass], output: TextIO, version: int):
    print(
        """\
// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

// THIS FILE IS AUTOGENERATED. DO NOT MODIFY

#pragma once""",
        file=output,
    )
    visit_all_classes(
        objects,
        [
            HeaderIncludes(output, [obj.name for obj in objects], version),
            HeaderDefineEnums(output),
            HeaderForwardDeclare(output),
            HeaderClasses(output),
        ],
    )
    print(
        f"""\
}}  // namespace movies::v{version}

namespace movies {{
  using namespace v{version};
}}  // namespace movies

#define MOVIES_CURR v{version}""",
        file=output,
    )
