from ...parser import file_pos
from ...model import WidlExtAttribute
import sys

simple_types = {
    "string": ["<string>", "std::u8string"],
    "ascii": ["<string>", "std::string"],
    "string_view": ["<string_view>", "std::u8string_view"],
    "path": ["<filesystem>", "std::filesystem::path"],
    "timestamp": ["<date/date.h>", "date::sys_seconds"],
    "int8_t": ["<cstdint>", "std::int8_t"],
    "int16_t": ["<cstdint>", "std::int16_t"],
    "int32_t": ["<cstdint>", "std::int32_t"],
    "int64_t": ["<cstdint>", "std::int64_t"],
    "uint8_t": ["<cstdint>", "std::uint8_t"],
    "uint16_t": ["<cstdint>", "std::uint16_t"],
    "uint32_t": ["<cstdint>", "std::uint32_t"],
    "uint64_t": ["<cstdint>", "std::uint64_t"],
    "conv_result": ["<json/serdes.hpp>", "json::conv_result"],
    "dict": ["<json/json.hpp>", "json::map"],
    "str_map": ["<map>", "std::map<std::u8string, std::u8string>"],
    "navigator": [None, "tangle::nav::navigator"],
    "uri": [None, "tangle::uri"],
}

builtin_types = [
    "void",
    "int",
    "unsigned",
    "short",
    "long long",
    "char",
    "bool",
    "alpha_2_aliases",
]


def error(pos: file_pos, msg: str):
    print(f"{pos}: error: {msg}", file=sys.stderr)
    sys.exit(1)


def flag_attribute(name: str, attr: WidlExtAttribute):
    if len(attr.args) != 0:
        error(attr.name.pos, "`{}' has no arguments".format(name))
    return True


def single_arg_attribute(name: str, values: list[str], attr: WidlExtAttribute):
    if len(attr.args) != 1:
        error(attr.name.pos, "`{}' requires exactly 1 argument".format(name))
    if attr.args[0].code not in values:
        error(
            attr.name.pos,
            "`{}' needs to be one of: {}".format(
                name, ", ".join(f"`{value}'" for value in values)
            ),
        )
    return attr.args[0].code


class SingleArg:
    def __init__(self, name: str, values: list[str], default: str):
        self.name = name
        self.values = values
        self.default = default

    def __call__(self, attr: WidlExtAttribute):
        return single_arg_attribute(self.name, self.values, attr)


class FlagArg:
    def __init__(self, name: str):
        self.name = name
        self.default = False

    def __call__(self, attr: WidlExtAttribute):
        return flag_attribute(self.name, attr)


class MergeWith:
    def __init__(self):
        self.name = "merge_with"
        self.default: list[tuple[str, str]] = []

    def __call__(self, attr: WidlExtAttribute):
        return [tuple(arg.s.split("|")) for arg in attr.args]


class LoadAs:
    def __init__(self):
        self.name = "load_as"
        self.default = None

    def __call__(self, attr: WidlExtAttribute):
        if len(attr.args) != 1:
            error(attr.name.pos, "`{}' requires exactly 1 argument".format(self.name))
        return attr.args[0].s


class Guard:
    def __init__(self):
        self.name = "guard"
        self.default = None

    def __call__(self, attr: WidlExtAttribute):
        if len(attr.args) != 1:
            error(attr.name.pos, "`{}' requires exactly 1 argument".format(self.name))
        return attr.args[0].s if attr.args[0].s is not None else attr.args[0].code


class Guards:
    def __init__(self):
        self.name = "guards"
        self.default = []

    def __call__(self, attr: WidlExtAttribute):
        if len(attr.args) > 0:
            error(attr.name.pos, "`{}' requires at least 1 argument".format(self.name))
        return [arg.s if arg.s is not None else arg.code for arg in attr.args]


class Default:
    def __init__(self):
        self.name = "default"
        self.default = ""

    def __call__(self, attr: WidlExtAttribute):
        if len(attr.args) != 1:
            error(attr.name.pos, "`{}' requires exactly 1 argument".format(self.name))
        return attr.args[0].s if attr.args[0].s is not None else attr.args[0].code


def _ext_attrs(ext_attrs: list[WidlExtAttribute], fields):
    result = {field.name: field.default for field in fields}
    fields = {field.name: field for field in fields}
    for attr in ext_attrs:
        code = attr.name.value.code
        try:
            field = fields[code]
        except KeyError:
            print(
                attr.name.pos,
                f"warning: unknown attribute `{attr.name.value.code}'",
                file=sys.stderr,
            )
            continue
        result[code] = field(attr)

    return result


_interface_ext_attrs = [
    SingleArg("from", ["node", "map"], "map"),
    SingleArg("merge", ["manual", "none", "auto"], "auto"),
    MergeWith(),
    FlagArg("spaceship"),
    FlagArg("load_postproc"),
    FlagArg("merge_postproc"),
]


def interface_ext_attrs(ext_attrs: list[WidlExtAttribute]):
    return _ext_attrs(ext_attrs, _interface_ext_attrs)


_attribute_ext_attrs = [FlagArg("or_value"), LoadAs(), Guard(), Guards(), Default()]


def attribute_ext_attrs(ext_attrs: list[WidlExtAttribute]):
    return _ext_attrs(ext_attrs, _attribute_ext_attrs)


_oparg_ext_attrs = [FlagArg("in"), FlagArg("out")]
_operation_ext_attrs = [
    FlagArg("mutable"),
    FlagArg("throws"),
    FlagArg("static"),
    Guard(),
    Guards(),
]


def oparg_ext_attrs(ext_attrs: list[WidlExtAttribute]):
    return _ext_attrs(ext_attrs, _oparg_ext_attrs)


def operation_ext_attrs(ext_attrs: list[WidlExtAttribute]):
    return _ext_attrs(ext_attrs, _operation_ext_attrs)
