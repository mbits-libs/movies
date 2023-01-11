from .common import *
from ...model import *
from typing import TextIO


class CollectTypes(ClassVisitor):
    def __init__(self):
        self.enums: set[str] = set()
        self.merge_with: dict[str, list[tuple[str, str]]] = {}

    def on_enum(self, obj: WidlEnum):
        self.enums.add(obj.name)

    def on_interface(self, obj: WidlInterface):
        ext_attrs = interface_ext_attrs(obj.ext_attrs)
        if len(ext_attrs["merge_with"]):
            self.merge_with[obj.name] = ext_attrs["merge_with"]


class ExtractSimpleType(TypeVisitor):
    def on_simple(self, obj: WidlSimple):
        return obj.text


def _simple(type: WidlType):
    return type.on_type_visitor(ExtractSimpleType())


class CodeDefineEnums(ClassVisitor):
    def __init__(self, output: TextIO):
        self.output = output

    def on_enum(self, obj: WidlEnum):
        x_macro = f"{obj.name.upper()}_X"
        print(
            f"""\
  template <>
  struct enum_traits<{obj.name}> : public enum_traits_helper<enum_traits<{obj.name}>, {obj.name}> {{
      using helper = enum_traits_helper<enum_traits<{obj.name}>, {obj.name}>;
      using name_type = helper::name_type;
      static std::span<name_type const> names() noexcept {{
        static constexpr name_type enum_names[] = {{
#define X_NAME(NAME) {{ u8 ## #NAME ## sv, {obj.name}::NAME }},
          {x_macro}(X_NAME)
#undef X_NAME
        }};
        return {{std::data(enum_names), std::size(enum_names)}};
      }}
  }};

""",
            file=self.output,
        )


class CodeClasses(ClassVisitor):
    def __init__(
        self,
        output: TextIO,
        enums: set[str],
        merge_with: dict[str, list[tuple[str, str]]],
        version: int,
    ):
        self.output = output
        self.enums = enums
        self.merge_with = merge_with
        self.version = version

    def on_interface(self, obj: WidlInterface):
        ext_attrs = interface_ext_attrs(obj.ext_attrs)
        load_from, merge_mode, merge_with, load_postproc, merge_postproc = (
            ext_attrs["from"],
            ext_attrs["merge"],
            ext_attrs["merge_with"],
            ext_attrs["load_postproc"],
            ext_attrs["merge_postproc"],
        )
        add_merge = merge_mode == "auto"
        add_serdes = load_from == "map"
        _spaces_ = " " * len(obj.name)
        if add_serdes:
            print(
                f"""\
  json::node {obj.name}::to_json() const {{
    json::map result{{}};""",
                file=self.output,
            )
            for prop in obj.props:
                or_value = attribute_ext_attrs(prop.ext_attrs)["or_value"]
                op = "store_or_value" if or_value else "store"
                print(
                    f'    v{self.version}::{op}(result, u8"{prop.name}"sv, {prop.name});',
                    file=self.output,
                )
            print(
                f"""\
    if (result.empty()) return {{}};
    return result;
  }}

  json::conv_result {obj.name}::from_json(json::map const& data,
                    {_spaces_}            std::string& dbg) {{
    auto result = json::conv_result::ok;""",
                file=self.output,
            )
            for prop in obj.props:
                ext_attrs = attribute_ext_attrs(prop.ext_attrs)
                or_value, load_as, empty = (
                    ext_attrs["or_value"],
                    ext_attrs["load_as"],
                    ext_attrs["empty"],
                )

                if load_as is not None:
                    print(
                        f"    OP({load_as});",
                        file=self.output,
                    )
                    continue
                op = (
                    "load_or_value"
                    if or_value
                    else "load_zero"
                    if empty == "allow"
                    else "load"
                )
                print(
                    f'    OP(v{self.version}::{op}(data, u8"{prop.name}", {prop.name}, dbg));',
                    file=self.output,
                )
            if load_postproc:
                print(
                    f"    OP(load_postproc(dbg));",
                    file=self.output,
                )
            print(
                """\
    return result;
  }
""",
                file=self.output,
            )

        if add_merge:
            print(
                """\
  json::conv_result {name}::merge({name} const& new_data{merge_with}) {{
    auto result = json::conv_result::ok;""".format(
                    name=obj.name,
                    merge_with="".join(
                        f", {arg_type} {arg_name}" for arg_type, arg_name in merge_with
                    ),
                ),
                file=self.output,
            )
            for prop in obj.props:
                simple_type = _simple(prop.type)
                params = self.merge_with.get(simple_type, [])
                merge_with = "".join(f", {arg[1]}" for arg in params)
                print(
                    f"    OP(v{self.version}::merge({prop.name}, new_data.{prop.name}{merge_with}));",
                    file=self.output,
                )
            if merge_postproc:
                print(
                    f"    OP(merge_postproc());",
                    file=self.output,
                )
            print(
                """\
    return result;
  }\n""",
                file=self.output,
            )


def print_code(objects: list[WidlClass], output: TextIO, header: str, version: int):
    types = CollectTypes()
    types.visit_all(objects)

    print(
        f"""\
// Copyright (c) 2023 midnightBITS
// This code is licensed under MIT license (see LICENSE for details)

// THIS FILE IS AUTOGENERATED. DO NOT MODIFY

#include {header}
#include "movie_info/impl.hpp"

namespace movies::v{version} {{""",
        file=output,
    )
    visit_all_classes(
        objects,
        [
            CodeDefineEnums(output),
            CodeClasses(output, types.enums, types.merge_with, version),
        ],
    )
    print(f"}}  // namespace movies::v{version}", file=output)
