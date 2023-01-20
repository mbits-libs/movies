from widl.printers.cpp import reorder_types
from widl.printers.python import CodeContext
from widl.model import *
from widl.parser import parse_all
import sys
import os
from contextlib import contextmanager
from widl.ver import VERSION


@contextmanager
def open_file(path: str):
    dirname = os.path.dirname(path)
    os.makedirs(dirname, exist_ok=True)
    with open(path, "w", encoding="UTF-8") as output:
        yield output


idl_paths = sys.argv[2:]
root = sys.argv[1]
code_path = os.path.join(root, "src", "py3", "movies_generated.cpp")
impl_path = os.path.join(root, "src", "py3", "movies_generated_impl.cpp")
stub_path = os.path.join(root, "modules", "movies.pyi")


objects = parse_all(idl_paths)
reorder_types(objects)


ctx = CodeContext(VERSION)
ctx.gather_from(objects)

with open_file(code_path) as output:
    ctx.output = output
    ctx.emit("code.mustache")

with open_file(impl_path) as output:
    ctx.output = output
    ctx.emit("code_impl.mustache")

with open_file(stub_path) as output:
    ctx.output = output
    ctx.emit("pyi.mustache")
