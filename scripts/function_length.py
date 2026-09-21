#!/usr/bin/env python3
"""Fail when a function spans more than the line limit.

Python uses the AST. Lua and C++ use brace depth, which matches the
functions in this repository. The limit is a screenful, not the length
of the longest function that already exists.
"""
import ast
import pathlib
import sys

LIMIT = 80
ROOTS = ("studio", "scripts", "tests", "src", "config")


def python_functions(path, source):
    try:
        tree = ast.parse(source)
    except SyntaxError as exc:
        return [f"{path}:{exc.lineno}: cannot parse"]
    found = []
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.end_lineno is not None:
            found.append((node.end_lineno - node.lineno + 1, node.lineno, node.name))
    return found


def brace_functions(path, source):
    found = []
    lines = source.splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if stripped.startswith(("//", "#", "--", "*", "/*")):
            index += 1
            continue
        name = signature_name(stripped)
        if name and stripped.endswith("{") and not stripped.endswith("};"):
            depth = 0
            for end in range(index, len(lines)):
                depth += lines[end].count("{") - lines[end].count("}")
                if end > index and depth <= 0:
                    found.append((end - index + 1, index + 1, name))
                    index = end
                    break
        index += 1
    return found


def signature_name(line):
    if "function" in line and line.endswith("end"):
        return None
    if line.startswith("local function ") or line.startswith("function "):
        head = line.split("(", 1)[0]
        return head.split()[-1]
    if "(" not in line or line.endswith(";"):
        return None
    head = line.split("(", 1)[0].strip()
    if not head or head.startswith(("if", "for", "while", "switch", "catch", "return")):
        return None
    token = head.split()[-1].split("::")[-1].lstrip("*&")
    if token in {"if", "for", "while", "switch"} or not token.replace("_", "").isalnum():
        return None
    return token


def scanned(path):
    source = path.read_text()
    if path.suffix == ".py":
        return python_functions(path, source)
    if path.suffix in {".lua", ".cpp", ".hpp"}:
        return brace_functions(path, source)
    return []


def main():
    failures = []
    for root in ROOTS:
        base = pathlib.Path(root)
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.suffix not in {".py", ".lua", ".cpp", ".hpp"} or "__pycache__" in path.parts:
                continue
            for item in scanned(path):
                if isinstance(item, str):
                    failures.append(item)
                    continue
                length, line, name = item
                if length > LIMIT:
                    failures.append(f"{path}:{line}: {name} is {length} lines (limit {LIMIT})")
    if failures:
        print("\n".join(sorted(failures, key=lambda item: (-int(item.split(" is ")[-1].split()[0]) if " is " in item else 0, item))))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
