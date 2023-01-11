from dataclasses import dataclass
from enum import Enum
from typing import Union


class Token(Enum):
    IDENT = 1
    NUMBER = 2
    STRING = 3
    OP = 4
    EOF = 5

    def __str__(self):
        return f"{self.name}".lower()


@dataclass
class file_pos:
    path: str
    line: int
    col: int

    def __str__(self):
        return f"{self.path}:{self.line}:{self.col}"


@dataclass
class partial_token:
    type: Token
    code: str
    s: Union[str, None] = None

    def __str__(self):
        if self.type == Token.STRING:
            return f'"{self.s}"'
        if self.type == Token.OP:
            return f"`{self.code}'"
        return f"{self.code}"


@dataclass
class token:
    value: partial_token
    pos: file_pos
