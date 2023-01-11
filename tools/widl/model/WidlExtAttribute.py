from ..parser.types import partial_token, token, file_pos


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
    def __init__(self, ext_attrs: list[WidlExtAttribute], pos: file_pos):
        self.ext_attrs = ext_attrs
        self.pos = pos
