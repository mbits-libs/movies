from typing import Union
import sys
import string
from .types import file_pos, Token, partial_token, token
from ..model import (
    ClassVisitor,
    WidlClass,
    WidlEnum,
    WidlInterface,
    WidlAttribute,
    WidlType,
    WidlOptional,
    WidlSequence,
    WidlTranslatable,
    WidlSimple,
    WidlExtAttribute,
    WidlOperation,
    WidlArgument,
)

FIRST_CHAR = string.ascii_letters + "_"
NEXT_CHAR = string.digits + string.ascii_letters + "_"


def _parse_error(pos: file_pos, msg: str):
    print(f"{pos}: error: {msg}", file=sys.stderr)
    raise RuntimeError()


def _skip_ws(line: str, col_no: int):
    while col_no < len(line) and line[col_no].isspace():
        col_no += 1
    return col_no


def _read_string(line: str, col_no: int):
    escape = False
    result = ""
    col_no += 1
    while col_no < len(line):
        if escape:
            escaped = line[col_no]
            try:
                escaped = {
                    "a": "\a",
                    "b": "\b",
                    "f": "\f",
                    "n": "\n",
                    "r": "\r",
                    "t": "\t",
                    "v": "\v",
                }[escaped]
            except KeyError:
                pass
            result += escaped
            escape = False
            col_no += 1
            continue
        if line[col_no] == "\\":
            escape = True
            col_no += 1
            continue
        if line[col_no] == '"':
            col_no += 1
            return col_no, result
        result += line[col_no]
        col_no += 1

    return col_no, None


def _append_token(
    tokens: list[token], type: Token, code: str, s: str = None, pos: file_pos = None
):
    tokens.append(token(partial_token(type, code, s), pos))


def tokenize(path: str) -> list[token]:
    result: list[token] = []
    line_no = 0
    last_len = 0
    with open(path, encoding="UTF-8") as f:
        for line in f:
            line_no += 1
            line = line.rstrip()
            last_len = len(line)
            col_no = 0
            while col_no < len(line):
                col_no = _skip_ws(line, col_no)
                if col_no == len(line):
                    break
                if line[col_no] == '"':
                    pos = file_pos(path, line_no, col_no + 1)
                    col_no, s = _read_string(line, col_no)
                    if s is None:
                        _parse_error(pos, "Error in string")
                    _append_token(result, Token.STRING, f'"{s}"', s, pos=pos)
                    continue

                if line[col_no] in FIRST_CHAR:
                    start = col_no
                    col_no += 1
                    pos = file_pos(path, line_no, col_no)
                    while col_no < len(line) and line[col_no] in NEXT_CHAR:
                        col_no += 1
                    _append_token(result, Token.IDENT, line[start:col_no], pos=pos)
                    continue

                if line[col_no] in string.digits:
                    start = col_no
                    col_no += 1
                    pos = file_pos(path, line_no, col_no)
                    while col_no < len(line) and line[col_no] in string.digits:
                        col_no += 1
                    _append_token(result, Token.NUMBER, line[start:col_no], pos=pos)
                    continue

                op = line[col_no]
                col_no += 1
                pos = file_pos(path, line_no, col_no)
                _append_token(result, Token.OP, op, pos=pos)

    _append_token(result, Token.EOF, "<eof>", pos=file_pos(path, line_no, last_len))
    return result


def ident(code: str):
    return partial_token(Token.IDENT, code)


def op(code: str):
    return partial_token(Token.OP, code)


class _Parser:
    def __init__(self, tokens: list[token]):
        self.index = -1
        self.tokens = tokens

    @property
    def eof(self):
        return self.index == len(self.tokens)

    @property
    def curr(self):
        return self.tokens[self.index] if not self.eof else self.tokens[-1]

    @property
    def peek(self):
        if self.index + 1 < len(self.tokens):
            return self.tokens[self.index + 1]
        return self.tokens[-1]

    def get(self):
        if self.eof:
            return None
        result = self.tokens[self.index]
        self.index += 1
        return result

    def next(self):
        if self.get() is None:
            tok = self.tokens[-1]
            print(f"{tok.pos}: error: expected token, got {tok.value}")
            raise RuntimeError()

    def put_back(self):
        if self.index:
            self.index -= 1

    def expect(self, *types: list[Union[partial_token, Token]]) -> token:
        tok = self.curr
        for T in types:
            if isinstance(T, partial_token):
                if tok.value == T:
                    return tok
            else:
                if tok.value.type == T:
                    return tok
        print(f"{tok.pos}: error: expected {types[0]}, got {tok.value}")
        raise RuntimeError()

    def consume(self, *types: list[Union[partial_token, Token]]):
        self.next()
        return self.expect(*types)


def _read_extended_attrs(parser: _Parser) -> list[WidlExtAttribute]:
    if parser.peek.value != op("["):
        return []

    result: list[WidlExtAttribute] = []
    parser.consume(op("["))
    while not parser.eof:
        name = parser.consume(Token.IDENT)
        if parser.peek.value == op("="):
            parser.get()
            value = parser.consume(Token.STRING, Token.NUMBER, Token.IDENT)
            result.append(WidlExtAttribute(name, [value.value]))
        elif parser.peek.value == op("("):
            parser.get()
            values: list[partial_token] = []
            while not parser.eof:
                value = parser.consume(Token.STRING, Token.NUMBER, Token.IDENT)
                values.append(value.value)
                tok = parser.consume(op(","), op(")"))
                if tok.value.code == ")":
                    break
            result.append(WidlExtAttribute(name, values))
        else:
            result.append(WidlExtAttribute(name, []))

        tok = parser.consume(op(","), op("]"))
        if tok.value.code == "]":
            break

    return result


def _read_enum(
    parser: _Parser, name: str, ext_attrs: list[WidlExtAttribute], pos: file_pos
) -> WidlClass:
    items: list[str] = []
    while not parser.eof:
        val = parser.consume(Token.STRING, op("}"))
        if val.value == op("}"):
            parser.put_back()
            return WidlEnum(name, items, ext_attrs, pos)
        items.append(val.value.s)
        tok = parser.consume(op(","), op("}"))
        if tok.value.code == "}":
            parser.put_back()
    return None


def _read_sequence(parser: _Parser) -> WidlType:
    parser.consume(op("<"))
    child = _read_type(parser)
    parser.consume(op(">"))
    return WidlSequence(child)


def _read_translatable(parser: _Parser) -> WidlType:
    parser.consume(op("<"))
    child = _read_type(parser)
    parser.consume(op(">"))
    return WidlTranslatable(child)


def _read_not_nullable_type(parser: _Parser) -> WidlType:
    type = parser.consume(Token.IDENT)
    if type.value.code == "long":
        if parser.peek.value == ident("long"):
            parser.get()
            return WidlSimple("long long")
        return WidlSimple("long")
    if type.value.code == "sequence":
        return _read_sequence(parser)
    if type.value.code == "translatable":
        return _read_translatable(parser)
    return WidlSimple(parser.curr.value.code)


def _read_type(parser: _Parser) -> WidlType:
    child = _read_not_nullable_type(parser)
    if parser.peek.value == op("?"):
        parser.get()
        return WidlOptional(child)
    return child


def _read_attribute(
    parser: _Parser, ext_attrs: list[WidlExtAttribute]
) -> WidlAttribute:
    type = _read_type(parser)
    name = parser.consume(Token.IDENT)
    parser.consume(op(";"))
    return WidlAttribute(name.value.code, type, ext_attrs, name.pos)


def _read_operation(
    parser: _Parser, ext_attrs: list[WidlExtAttribute]
) -> WidlOperation:
    args: list[WidlArgument] = []
    type = _read_type(parser)
    name = parser.consume(Token.IDENT)
    parser.consume(op("("))
    while not parser.eof:
        arg_ext_attrs = _read_extended_attrs(parser)
        tok = parser.consume(op(")"), Token.IDENT)
        parser.put_back()
        if tok == op(")"):
            break
        arg_type = _read_type(parser)
        arg_name = parser.consume(Token.IDENT)
        args.append(
            WidlArgument(arg_name.value.code, arg_type, arg_ext_attrs, arg_name.pos)
        )
        tok = parser.consume(op(","), op(")"))
        if tok.value.code == ")":
            parser.put_back()
            break
    parser.consume(op(")"))
    parser.consume(op(";"))
    return WidlOperation(name.value.code, type, args, ext_attrs, name.pos)


def _read_interface(
    parser: _Parser, name: str, ext_attrs: list[WidlExtAttribute], pos: file_pos
) -> WidlClass:
    attribs: list[WidlAttribute] = []
    operations: list[WidlOperation] = []
    while not parser.eof:
        member_ext_attrs = _read_extended_attrs(parser)
        tok = parser.consume(ident("attribute"), op("}"), Token.IDENT)
        code = tok.value.code
        if code == "}":
            parser.put_back()
            return WidlInterface(name, attribs, operations, ext_attrs, pos)
        if code == "attribute":
            attr = _read_attribute(parser, member_ext_attrs)
            attribs.append(attr)
            continue
        if tok.value.type == Token.IDENT:
            parser.put_back()
            oper = _read_operation(parser, member_ext_attrs)
            operations.append(oper)

    return None


def _is_partial(parser: _Parser):
    if parser.peek.value == ident("partial"):
        parser.consume(ident("partial"))
        return True
    return False


_TL_READERS: dict[str, callable] = {"enum": _read_enum, "interface": _read_interface}


def _top_level_items(parser: _Parser) -> list[WidlClass]:
    result: list[WidlClass] = []
    while not parser.eof:
        iface_ext_attrs = _read_extended_attrs(parser)
        partial = _is_partial(parser)
        type = parser.consume(Token.IDENT, Token.EOF)
        if type.value.type == Token.EOF:
            break
        name = parser.consume(Token.IDENT)
        parser.consume(op("{"))

        try:
            reader = _TL_READERS[type.value.code]
        except:
            print(f'{type.pos}: error: unexpected "{type.value} {name.value}"')
            raise RuntimeError()

        klass = reader(parser, name.value.code, iface_ext_attrs, name.pos)
        klass.partial = partial
        result.append(klass)
        if partial and type.value.code != "interface":
            print(f"{type.pos}: error: only interfaces can be partial")
            raise RuntimeError()

        parser.consume(op("}"))
        parser.consume(op(";"))
    return result


class GetKind(ClassVisitor):
    def __init__(self):
        self.kind = None

    def on_enum(self, _: WidlEnum):
        self.kind = "enum"

    def on_interface(self, _: WidlInterface):
        self.kind = "interface"


def _get_kind(object: WidlClass):
    return GetKind().visit_one(object).kind


def _merge(prev: WidlInterface, next: WidlInterface):
    props = set(prop.name for prop in prev.props)
    ops = set(op.name for op in prev.ops)
    for prop in next.props:
        if prop.name in props:
            print(
                f"{next.pos}: error: definition of {prev.name}.{prop.name} found in two places"
            )
            print(f"{prev.pos}: info: see previous definition")
            raise RuntimeError()
        prev.props.append(prop)
    for op in next.ops:
        if op.name in ops:
            print(
                f"{next.pos}: error: definition of {prev.name}.{op.name} found in two places"
            )
            print(f"{prev.pos}: info: see previous definition")
            raise RuntimeError()
        prev.ops.append(op)
    # prev.ext_attrs.update(next.ext_attrs)


def top_level_items(tokens: list[token]) -> list[WidlClass]:
    return _top_level_items(_Parser(tokens))


def parse(path: str):
    return top_level_items(tokenize(path))


def parse_all(paths: list[str]) -> list[WidlClass]:
    known: dict[str, tuple[str, WidlClass]] = {}
    order: list[str] = []
    for path in paths:
        local = parse(path)
        for object in local:
            kind = _get_kind(object)
            prev = known.get(object.name)
            if prev is None:
                known[object.name] = (kind, object)
                order.append(object.name)
                continue
            prev_kind, prev_object = prev
            if prev_kind != kind:
                print(
                    f"{object.pos}: error: `{object.name}' was `{prev_kind}' and now is `{kind}'"
                )
                print(f"{prev_object.pos}: note: see previous definition")
                raise RuntimeError()
            if not prev_object.partial and not object.partial:
                print(
                    f"{object.pos}: error: neither `{object.name}' was declared partial"
                )
                print(f"{prev_object.pos}: note: see previous definition")
                raise RuntimeError()
            _merge(prev_object, object)

    return [known[name][1] for name in order]
