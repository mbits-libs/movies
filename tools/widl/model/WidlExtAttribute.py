from ..types import partial_token, token, file_pos
import sys
from typing import Callable


class WidlExtAttribute:
    def __init__(self, name: token, arguments: list[partial_token]):
        self.name = name
        self.args = arguments

    def __str__(self):
        if len(self.arg) == 0:
            return f"{self.name}"
        if len(self.arg) == 1:
            return f"{self.name}={self.args[0]}"
        return "{}({})".format(self.name, ", ".join(str(arg) for arg in self.args))


class WidlExtendable:
    def __init__(
        self,
        ext_attrs: list[WidlExtAttribute],
        transform: Callable[[list[WidlExtAttribute]], dict],
        pos: file_pos,
    ):
        self.ext_attrs = transform(ext_attrs)
        self.pos = pos


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


class AttrMergeWith:
    def __init__(self):
        self.name = "merge_with"
        self.default: list[str] = []

    def __call__(self, attr: WidlExtAttribute):
        return [arg.s for arg in attr.args]


class StringArg:
    def __init__(self, name: str):
        self.name = name
        self.default = None

    def __call__(self, attr: WidlExtAttribute):
        if len(attr.args) != 1:
            error(attr.name.pos, "`{}' requires exactly 1 argument".format(self.name))
        print(f"visiting {self.name} with {attr.args[0]}")
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
        if len(attr.args) < 0:
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


def enum_ext_attrs(_: list[WidlExtAttribute]):
    return {}


_interface_ext_attrs = [
    SingleArg("from", ["node", "map", "none"], "map"),
    SingleArg("merge", ["manual", "none", "auto"], "auto"),
    MergeWith(),
    FlagArg("spaceship"),
    FlagArg("load_postproc"),
    FlagArg("merge_postproc"),
    FlagArg("nonjson"),
    StringArg("cpp_quote"),
]


def interface_ext_attrs(ext_attrs: list[WidlExtAttribute]):
    return _ext_attrs(ext_attrs, _interface_ext_attrs)


_attribute_ext_attrs = [
    SingleArg("empty", ["warn", "allow"], "warn"),
    FlagArg("or_value"),
    StringArg("load_as"),
    Guard(),
    Guards(),
    Default(),
    AttrMergeWith(),
]


def attribute_ext_attrs(ext_attrs: list[WidlExtAttribute]):
    return _ext_attrs(ext_attrs, _attribute_ext_attrs)


_oparg_ext_attrs = [FlagArg("defaulted"), FlagArg("in"), FlagArg("out")]
_operation_ext_attrs = [
    FlagArg("mutable"),
    FlagArg("throws"),
    FlagArg("static"),
    FlagArg("external"),
    Guard(),
    Guards(),
]


def oparg_ext_attrs(ext_attrs: list[WidlExtAttribute]):
    return _ext_attrs(ext_attrs, _oparg_ext_attrs)


def operation_ext_attrs(ext_attrs: list[WidlExtAttribute]):
    return _ext_attrs(ext_attrs, _operation_ext_attrs)
