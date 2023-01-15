from typing import TextIO, Union
from pprint import pprint
from contextlib import contextmanager
import os
import inspect
import json

CONST = 0
PUSH = 1
PUSH_NOT = 2
PUSH_IF = 3
POP = 4
EMIT = 5
ENDLINE = 6
WITH = 7
LINE_CONT = 8
REMOVED_ENDLINE = 9

NAMES = {
    CONST: "CONST",
    PUSH: "PUSH",
    PUSH_NOT: "PUSH_NOT",
    PUSH_IF: "PUSH_IF",
    POP: "POP",
    EMIT: "EMIT",
    ENDLINE: "ENDLINE",
    WITH: "WITH",
    LINE_CONT: "LINE_CONT",
    REMOVED_ENDLINE: "REMOVED_ENDLINE",
}


WITH_SWITCH = 0
WITH_TRUTHY = 1
WITH_FALSY = 2

Ref = Union[str, int]


def _extract_simple(context: any, sel: str):
    if isinstance(context, dict):
        return context.get(sel)
    if hasattr(context, sel):
        return getattr(context, sel)
    return None


def _extract_one(contexts: list, sel: str):
    for context in reversed(contexts):
        result = _extract_simple(context, sel)
        if result is not None:
            return result
    return None


def extract_value(contexts: list, sel: list[str]):
    # print(f'+ {".".join(sel)}')
    for name in sel:
        sub = _extract_one(contexts, name)
        if sub is None:
            return None
        contexts = [*contexts, sub]
    return contexts[-1] if len(contexts) else None


def _seg(ref: Ref):
    if isinstance(ref, int):
        return f"({ref})"
    return str(ref)


class Context:
    def __init__(self, root, debug: bool):
        self.debug = debug
        self.root = root
        self.path: list[tuple[Ref, any]] = []

    def enter(self, addr: list[Ref]):
        first = True
        for name_or_id in addr:
            self._move_to(name_or_id, first)
            first = False
        return self.current

    def leave(self, num: int):
        self.path = self.path[:-num]

    def _move_to(self, name_or_id: Ref, first: bool):
        if isinstance(name_or_id, int):
            return self._move_to_int(name_or_id)
        return self._move_to_str(name_or_id, first)

    def _move_to_str(self, ref: str, first: bool):
        if first:
            if ref == "":
                self.path.append((ref, self.root))
                return

            # 1 - look for list self-reference
            for index in range(len(self.path), 0, -1):
                name, current = self.path[index - 1]
                if (
                    not isinstance(current, list)
                    or name != ref
                    or index == len(self.path)
                ):
                    continue

                index_ref, item = self.path[index]
                if not isinstance(index_ref, int):
                    continue

                self.path.append((ref, item))
                return

            # 2 - look for object self-reference
            for index in range(len(self.path), 0, -1):
                name, current = self.path[index - 1]
                if name == ref:
                    self.path.append((ref, current))
                    return

        # 3 - look for attribute
        for index in range(len(self.path), 0, -1):
            current = self.path[index - 1][1]
            if isinstance(current, dict):
                next = current.get(ref)
                if next is not None:
                    self.path.append((ref, next))
                    return

            if hasattr(current, ref):
                self.path.append((ref, getattr(current, ref)))
                return

        self.path.append((ref, None))

    def _move_to_int(self, ref: int):
        for index in range(len(self.path), 0, -1):
            current = self.path[index - 1][1]
            if not isinstance(current, list):
                continue
            next = current[ref]
            self.path.append((ref, next))
            return
        self.path.append((ref, None))

    @property
    def current(self):
        if not len(self.path):
            return self.root
        return self.path[-1][1]


def _dbg(value):
    return (
        " -> {...}"
        if isinstance(value, dict)
        else " -> [...]"
        if isinstance(value, list) and len(value) > 0
        else " -> []"
        if isinstance(value, list) and len(value) == 0
        else f" -> {value!r}"
        if isinstance(value, (bool, int, str))
        else f" -> None"
        if value is None
        else ""
    )


def _json(value):
    if isinstance(value, list):
        return [_json(item) for item in value]
    if isinstance(value, dict):
        return {key: _json(value[key]) for key in value}
    if hasattr(value, "__dict__"):
        return _json(vars(value))
    return value


@contextmanager
def enter(ctx: Context, name: Ref):
    addr = [name] if isinstance(name, int) else name.split(".")
    current = ctx.enter(addr)
    if ctx.debug:
        print(f"+ {'/'.join(_seg(x) for x, _ in ctx.path)}{_dbg(current)}")
    yield current
    ctx.leave(len(addr))


def extract(ctx: Context, addr: list[str]):
    current = ctx.enter(addr)
    if ctx.debug:
        print(f"> {'/'.join(_seg(x) for x, _ in ctx.path)}{_dbg(current)}")
    ctx.leave(len(addr))
    return current


class Template:
    def __init__(self, name: str, aspect: int):
        self.name = name
        self.aspect = aspect
        self.code: list[(int, any)] = []

    def op(self, code: int, arg: any):
        self.code.append((code, arg))

    def clean(self):
        new_code: list[(int, any)] = []
        for index in range(len(self.code)):
            op, arg = self.code[index]
            if op == REMOVED_ENDLINE:
                continue
            if op == ENDLINE and index == 0:
                continue
            if op == ENDLINE and index > 0:
                prev = self.code[index - 1]
                if prev[0] in [WITH, LINE_CONT]:
                    continue
            if op == LINE_CONT:
                max_index = len(self.code) - 1
                if index < max_index and self.code[index + 1][0] == ENDLINE:
                    self.code[index + 1] = (REMOVED_ENDLINE, None)
                    if index + 1 < max_index and self.code[index + 2][0] == CONST:
                        self.code[index + 2] = (CONST, self.code[index + 2][1].lstrip())

            if op == WITH:
                arg.clean()
            new_code.append((op, arg))
        self.code = new_code

    def run(self, context: Context) -> str:
        with enter(context, self.name) as this_context:
            no_context = not this_context
            if no_context != (self.aspect == WITH_FALSY):
                return ""

            if isinstance(this_context, list) and self.aspect == WITH_SWITCH:
                text = []
                for index in range(len(this_context)):
                    with enter(context, index):
                        text.append(self._run_single(context))
                return "".join(text)

            return self._run_single(context)

    def _run_single(self, context: Context) -> str:
        text = []
        for op, arg in self.code:
            if op == WITH:
                text.append(arg.run(context))
                continue
            if op == CONST:
                text.append(str(arg))
                continue
            if op == EMIT:
                value = extract(context, arg)
                if value is not None:
                    text.append(str(value))
                continue
            if op == ENDLINE:
                text.append("\n")
                continue

        return "".join(text)

    def print(self, prefix=""):
        for op, arg in self.code:
            if op == WITH:
                arg_op = (
                    ""
                    if arg.aspect == WITH_SWITCH
                    else "? "
                    if arg.aspect == WITH_TRUTHY
                    else "! "
                )
                print(f"{prefix}{arg_op}{arg.name} {{")
                arg.print(prefix + "     ")
                print(f"{prefix}}}")
                continue
            if op == CONST:
                print(f"{prefix}: {arg!r}")
                continue
            if op == EMIT:
                print(f"{prefix}> {'.'.join(arg)}")
                continue
            if op == ENDLINE:
                # print(f"{prefix}\u2B92")
                continue
            print(f"{prefix}{op} {arg}")


def render(template: str, context: any, file: TextIO, debug: bool):
    if debug:
        print(json.dumps(_json(context), sort_keys=True, indent=4))
    code = []
    dbg = []
    lines = template.split("\n")
    for line in lines:
        dbg_line = []
        chunks = line.rstrip("\n").split("{{")
        if len(chunks[0]):
            code.append((CONST, chunks[0]))
            dbg_line.append(chunks[0])
        for chunk in chunks[1:]:
            arg, const = chunk.split("}}", 1)
            dbg_line.append((arg, const))
            if arg.strip() == "\\":
                code.append((LINE_CONT, None))
                continue
            next_op = EMIT
            if len(arg):
                next_op = {"#": PUSH, "?": PUSH_IF, "^": PUSH_NOT, "/": POP}.get(
                    arg[0], EMIT
                )
                if next_op != EMIT:
                    arg = arg[1:]
            arg = [name.strip() for name in arg.split(".")]
            code.append((next_op, arg))
            if len(const):
                code.append((CONST, const))
        code.append((ENDLINE, None))
        dbg.append(dbg_line)

    if code[-1][0] == ENDLINE:
        code = code[:-1]
    if debug:
        pprint([(NAMES[op], arg) for op, arg in code])

    templates = [Template("", WITH_SWITCH)]
    for op, arg in code:
        if op in [PUSH, PUSH_NOT, PUSH_IF]:
            subctx = Template(
                ".".join(arg),
                WITH_SWITCH
                if op == PUSH
                else WITH_TRUTHY
                if op == PUSH_IF
                else WITH_FALSY,
            )
            templates[-1].op(WITH, subctx)
            templates.append(subctx)
            continue
        if op == POP:
            name = ".".join(arg)
            if templates[-1].name != name:
                raise RuntimeError(f"Cannot close {templates[-1].name} with {name}")
            if len(templates) == 1:
                raise RuntimeError(f"Cannot close root context with {name}")
            templates.pop()
            continue
        templates[-1].op(op, arg)
    template = templates[-1]
    template.clean()

    if debug:
        template.print()
    text = template.run(Context(context, debug))
    print(text, file=file, end="")


class TemplateContext:
    def __init__(self, output: TextIO, version: int):
        self.output = output
        self.version = version

    def debug_emit(self, filename: str):
        self.emit(filename, True)

    def emit(self, filename: str, debug: bool = False):
        file = inspect.getfile(self.__class__)

        with open(
            os.path.join(os.path.dirname(file), filename), encoding="UTF-8"
        ) as mustache:
            render(mustache.read(), context=self, file=self.output, debug=debug)
