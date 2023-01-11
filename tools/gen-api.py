from widl.printers.cpp import print_code, print_header, reorder_types
from widl.model import *
from widl.parser import parse
import sys
import os
from contextlib import contextmanager

VERSION = 1


@contextmanager
def open_file(path: str):
    dirname = os.path.dirname(path)
    os.makedirs(dirname, exist_ok=True)
    with open(path, "w", encoding="UTF-8") as output:
        yield output


idl_path = sys.argv[1]
root = sys.argv[2]
code_path = os.path.join(root, "src", "movie_info_generated.cpp")
header_path = os.path.join(root, "inc", "movies", "movie_info.hpp")


objects = parse(idl_path)
reorder_types(objects)
with open_file(code_path) as output:
    print_code(objects, output, "<movies/movie_info.hpp>", VERSION)
with open_file(header_path) as output:
    print_header(objects, output, VERSION)
