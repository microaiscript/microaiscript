#!/usr/bin/env python3
"""Extract AiScript conformance fixtures from aiscript-rs v1 tests.

Walks aiscript-rs/aiscript-v1/tests/*.rs and emits one JSON file per module
under test/conformance/. Re-run after updating the vendored Rust suite.
"""

from __future__ import annotations

import json
import os
import re
import sys
from typing import Any

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RS_TESTS = os.path.join(ROOT, "aiscript-rs", "aiscript-v1", "tests")
OUT_DIR = os.path.dirname(os.path.abspath(__file__))

FILE_TAGS = {
    "index": "core",
    "literals": "core",
    "syntax": "core",
    "jump_statements": "core",
    "newline": "core",
    "types": "core",
    "std": "stdlib",
    "primitive_props": "primitive_props",
    "interpreter": "host",
    "identifiers": "parser",
}

SKIP_FILES = {"identifiers"}  # macro-generated parser matrices


class ParseError(Exception):
    pass


class Scanner:
    def __init__(self, src: str):
        self.src = src
        self.n = len(src)
        self.i = 0

    def peek(self, n: int = 1) -> str:
        return self.src[self.i : self.i + n]

    def eof(self) -> bool:
        return self.i >= self.n

    def skip_ws_and_comments(self) -> None:
        while not self.eof():
            c = self.src[self.i]
            if c in " \t\r\n":
                self.i += 1
                continue
            if self.peek(2) == "//":
                while not self.eof() and self.src[self.i] != "\n":
                    self.i += 1
                continue
            if self.peek(2) == "/*":
                end = self.src.find("*/", self.i + 2)
                self.i = self.n if end < 0 else end + 2
                continue
            break

    def startswith(self, s: str) -> bool:
        return self.src.startswith(s, self.i)


def parse_rust_string(s: Scanner) -> str:
    s.skip_ws_and_comments()
    if s.eof():
        raise ParseError("expected string")
    if s.src[s.i] == "r":
        s.i += 1
        hashes = 0
        while not s.eof() and s.src[s.i] == "#":
            hashes += 1
            s.i += 1
        if s.eof() or s.src[s.i] != '"':
            raise ParseError("expected raw string")
        s.i += 1
        close = '"' + ("#" * hashes)
        end = s.src.find(close, s.i)
        if end < 0:
            raise ParseError("unterminated raw string")
        val = s.src[s.i : end]
        s.i = end + len(close)
        return val
    if s.src[s.i] != '"':
        raise ParseError("expected string literal")
    s.i += 1
    out = []
    while not s.eof():
        c = s.src[s.i]
        if c == '"':
            s.i += 1
            return "".join(out)
        if c == "\\":
            s.i += 1
            if s.eof():
                raise ParseError("unterminated string")
            e = s.src[s.i]
            s.i += 1
            mapping = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", '"': '"', "'": "'", "\\": "\\"}
            if e in mapping:
                out.append(mapping[e])
            elif e == "u":
                if s.peek() == "{":
                    s.i += 1
                    hexpart = ""
                    while not s.eof() and s.src[s.i] != "}":
                        hexpart += s.src[s.i]
                        s.i += 1
                    if not s.eof() and s.src[s.i] == "}":
                        s.i += 1
                    out.append(chr(int(hexpart, 16)))
                else:
                    hexpart = s.src[s.i : s.i + 4]
                    s.i += 4
                    out.append(chr(int(hexpart, 16)))
            else:
                out.append(e)
        else:
            out.append(c)
            s.i += 1
    raise ParseError("unterminated string")


def parse_value(s: Scanner) -> dict[str, Any]:
    s.skip_ws_and_comments()
    ident = re.match(r"[A-Za-z_][A-Za-z0-9_]*", s.src[s.i :])
    if ident:
        name = ident.group(0)
        s.i += len(name)
        s.skip_ws_and_comments()
        if name == "null":
            if s.peek() == "(":
                s.i += 1
                s.skip_ws_and_comments()
                if s.peek() != ")":
                    raise ParseError("expected ) after null(")
                s.i += 1
            return {"type": "null"}
        if name in ("true", "false") and s.peek() != "(":
            return {"type": "bool", "value": name == "true"}
        if s.peek() != "(":
            raise ParseError(f"expected ( after {name}")
        s.i += 1
        s.skip_ws_and_comments()
        if name == "bool":
            val = parse_value(s)
            s.skip_ws_and_comments()
            if s.peek() != ")":
                raise ParseError("expected ) after bool(")
            s.i += 1
            if val.get("type") == "bool":
                return val
            raise ParseError("bad bool()")
        if name == "num":
            start = s.i
            if s.peek() in "+-":
                s.i += 1
            while not s.eof() and (s.src[s.i].isdigit() or s.src[s.i] in ".eE+-"):
                s.i += 1
            num_txt = s.src[start : s.i].strip()
            s.skip_ws_and_comments()
            if s.peek() != ")":
                raise ParseError("expected ) after num(")
            s.i += 1
            return {"type": "num", "value": float(num_txt)}
        if name == "str":
            val = parse_rust_string(s)
            s.skip_ws_and_comments()
            if s.peek() != ")":
                raise ParseError("expected ) after str(")
            s.i += 1
            return {"type": "str", "value": val}
        if name == "arr":
            inner = parse_list(s)
            s.skip_ws_and_comments()
            if s.peek() != ")":
                raise ParseError("expected ) after arr(")
            s.i += 1
            return {"type": "arr", "value": inner}
        if name == "obj":
            obj = parse_obj_arg(s)
            s.skip_ws_and_comments()
            if s.peek() != ")":
                raise ParseError("expected ) after obj(")
            s.i += 1
            return {"type": "obj", "value": obj}
        if name == "error":
            ename = parse_rust_string(s)
            s.skip_ws_and_comments()
            info = None
            if s.peek() == ",":
                s.i += 1
                s.skip_ws_and_comments()
                if s.startswith("None"):
                    s.i += 4
                    info = None
                elif s.startswith("Some"):
                    s.i += 4
                    s.skip_ws_and_comments()
                    if s.peek() != "(":
                        raise ParseError("expected ( after Some")
                    s.i += 1
                    info = parse_value(s)
                    s.skip_ws_and_comments()
                    if s.peek() != ")":
                        raise ParseError("expected ) after Some(")
                    s.i += 1
            s.skip_ws_and_comments()
            if s.peek() != ")":
                raise ParseError("expected ) after error(")
            s.i += 1
            out: dict[str, Any] = {"type": "error", "name": ename}
            if info is not None:
                out["info"] = info
            return out
        raise ParseError(f"unknown constructor {name}")
    if s.peek() in "'\"" or s.startswith("r") or s.startswith("r#"):
        return {"type": "str", "value": parse_rust_string(s)}
    if s.peek() in "+-" or (not s.eof() and s.src[s.i].isdigit()):
        start = s.i
        if s.peek() in "+-":
            s.i += 1
        while not s.eof() and (s.src[s.i].isdigit() or s.src[s.i] in ".eE"):
            s.i += 1
        return {"type": "num", "value": float(s.src[start : s.i])}
    raise ParseError(f"expected value at {s.i}: {s.src[s.i:s.i+40]!r}")


def parse_list(s: Scanner) -> list[Any]:
    s.skip_ws_and_comments()
    if s.peek() != "[":
        raise ParseError("expected [")
    s.i += 1
    items: list[Any] = []
    while True:
        s.skip_ws_and_comments()
        if s.peek() == "]":
            s.i += 1
            return items
        items.append(parse_value(s))
        s.skip_ws_and_comments()
        if s.peek() == ",":
            s.i += 1
            continue
        if s.peek() == "]":
            s.i += 1
            return items
        raise ParseError("expected , or ] in array")


def parse_obj_arg(s: Scanner) -> dict[str, Any]:
    s.skip_ws_and_comments()
    if s.peek() != "[":
        raise ParseError("expected [ for obj")
    s.i += 1
    s.skip_ws_and_comments()
    result: dict[str, Any] = {}
    if s.startswith("]"):
        s.i += 1
        s.skip_ws_and_comments()
        if s.startswith("as "):
            while not s.eof() and s.peek() != ")":
                s.i += 1
        return result
    while True:
        s.skip_ws_and_comments()
        if s.peek() == "]":
            s.i += 1
            return result
        if s.peek() != "(":
            raise ParseError("expected (key, val) in obj")
        s.i += 1
        key = parse_rust_string(s)
        s.skip_ws_and_comments()
        if s.peek() != ",":
            raise ParseError("expected comma in obj pair")
        s.i += 1
        val = parse_value(s)
        s.skip_ws_and_comments()
        if s.peek() != ")":
            raise ParseError("expected ) after obj pair")
        s.i += 1
        result[key] = val
        s.skip_ws_and_comments()
        if s.peek() == ",":
            s.i += 1
            continue
        if s.peek() == "]":
            s.i += 1
            return result
        raise ParseError("expected , or ] in obj")


def classify_error(after: str) -> str:
    head = after[:800]
    if "AiScriptSyntaxError" in head or "AiScriptError::Syntax" in head:
        return "syntax"
    if "AiScriptRuntimeError" in head or "AiScriptError::Runtime" in head:
        return "runtime"
    if "unwrap_err" in head:
        return "runtime"
    return "runtime"


def looks_like_error_call(after_call: str) -> bool:
    """after_call starts at the closing ')' of test(...)."""
    return bool(re.match(r"\)\s*(?:\.await\s*)?\.unwrap_err\b", after_call, re.S))


def extract_file(path: str, module: str) -> list[dict[str, Any]]:
    src = open(path, encoding="utf-8").read()
    cases: list[dict[str, Any]] = []
    tag = FILE_TAGS.get(module, "core")
    xfail_vm = tag in ("primitive_props",)
    skip_vm = tag in ("host", "parser")
    skip_interp = tag == "host"

    mod_stack: list[str] = [module]
    fn_name = "top"
    i = 0
    n = len(src)
    case_i = 0

    while i < n:
        # track modules
        mmod = re.match(r"\bmod\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{", src[i:])
        mfn = re.match(r"(?:async\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", src[i:])
        if src.startswith("test(", i) and not src.startswith("testutils", i):
            # skip the helper in testutils
            if "/tests/testutils/" in path.replace("\\", "/"):
                i += 1
                continue
            s = Scanner(src)
            s.i = i + 4  # at '('
            s.skip_ws_and_comments()
            if s.peek() != "(":
                i += 1
                continue
            s.i += 1
            try:
                s.skip_ws_and_comments()
                program = parse_rust_string(s)
            except ParseError:
                i += 1
                continue
            s.skip_ws_and_comments()
            if s.peek() != ",":
                i += 1
                continue
            s.i += 1
            # skip closure until matching paren of test(
            depth = 1
            start_rest = s.i
            while s.i < s.n and depth > 0:
                ch = s.src[s.i]
                if ch in "\"'":
                    try:
                        parse_rust_string(s)
                        continue
                    except ParseError:
                        s.i += 1
                        continue
                if ch == "r" and (s.peek(2).startswith("r#") or s.peek(2).startswith('r"')):
                    try:
                        parse_rust_string(s)
                        continue
                    except ParseError:
                        s.i += 1
                        continue
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        break
                s.i += 1
            closure_src = src[start_rest : s.i]
            after = src[s.i : s.i + 80]
            is_err = looks_like_error_call(src[s.i :])
            case_i += 1
            name = f"{'::'.join(mod_stack)}::{fn_name}#{case_i}"
            case: dict[str, Any] = {
                "name": name,
                "program": program,
                "tags": [tag],
            }
            if skip_vm:
                case["skip_vm"] = True
            elif xfail_vm:
                case["xfail_vm"] = True
            elif any(m in program for m in (
                "Core:", "Util:", "Math:", "Json:", "Date:", "Num:", "Str:",
                "Arr:", "Obj:", "Error:", "Async:",
            )):
                case["xfail_vm"] = True
            if skip_interp:
                case["skip_interpreter"] = True
            if is_err:
                case["expect_error"] = classify_error(src[s.i : s.i + 1200])
            else:
                m = re.search(r"assert_eq!\s*\(\s*res\s*,", closure_src)
                if m:
                    vs = Scanner(closure_src)
                    vs.i = m.end()
                    try:
                        case["expect"] = parse_value(vs)
                    except ParseError as e:
                        case["extract_error"] = str(e)
                        case["skip_interpreter"] = True
                        case["skip_vm"] = True
                else:
                    # parser/empty or unasserted
                    case["skip_interpreter"] = True
                    case["skip_vm"] = True
                    case["extract_error"] = "no assert_eq"
            cases.append(case)
            i = s.i + 1
            continue
        if mmod:
            # naive brace matching for module names of tests
            mod_stack.append(mmod.group(1))
            i += mmod.end()
            continue
        if mfn:
            fn_name = mfn.group(1)
            i += mfn.end()
            continue
        if src[i] == "}":
            if len(mod_stack) > 1:
                # only pop if this closes a mod; heuristic: previous non-ws is not needed;
                # keep stack from running away by also tracking braces.
                pass
            i += 1
            continue
        i += 1

    # Rebuild module names with a brace-accurate pass
    return refine_names(src, cases, module)


def refine_names(src: str, cases: list[dict[str, Any]], module: str) -> list[dict[str, Any]]:
    """Assign better names from nearest async fn / mod using line numbers."""
    lines = src.splitlines()
    mods: list[tuple[int, str]] = []
    fns: list[tuple[int, str]] = []
    brace = 0
    stack: list[tuple[str, int]] = [(module, 0)]
    for ln, line in enumerate(lines, 1):
        stripped = line.strip()
        mmod = re.match(r"mod\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{", stripped)
        mfn = re.match(r"(?:pub\s+)?(?:async\s+)?fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", stripped)
        if mmod:
            stack.append((mmod.group(1), brace + stripped.count("{") - stripped.count("}")))
            mods.append((ln, mmod.group(1)))
        if mfn:
            fns.append((ln, mfn.group(1)))
        brace += line.count("{") - line.count("}")
        while len(stack) > 1 and brace < stack[-1][1]:
            stack.pop()

    # locate each case by unique program occurrence
    used: dict[str, int] = {}
    for case in cases:
        pos = 0
        key = case["program"]
        # find occurrence after previous
        idx = src.find("test(", used.get("p", 0))
        used["p"] = idx + 4 if idx >= 0 else used.get("p", 0)
        line_no = src.count("\n", 0, max(idx, 0)) + 1
        fn = "case"
        for ln, name in fns:
            if ln <= line_no:
                fn = name
            else:
                break
        mod_name = module
        # find enclosing mods via a simpler scan up to line_no
        stack2: list[tuple[str, int]] = [module]
        b = 0
        for ln, line in enumerate(lines, 1):
            if ln > line_no:
                break
            st = line.strip()
            mm = re.match(r"mod\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{", st)
            delta = line.count("{") - line.count("}")
            if mm:
                stack2.append(mm.group(1))
            b += delta
        # reconstruct from line scan
        stack2 = [module]
        depths = [0]
        b = 0
        for ln, line in enumerate(lines, 1):
            if ln > line_no:
                break
            st = line.strip()
            mm = re.match(r"mod\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{", st)
            opens = line.count("{")
            closes = line.count("}")
            if mm:
                stack2.append(mm.group(1))
                depths.append(b + opens - closes)
            b += opens - closes
            while len(stack2) > 1 and b < depths[-1]:
                stack2.pop()
                depths.pop()
        case["name"] = f"{'::'.join(stack2)}::{fn}"
        # disambiguate duplicates later

    counts: dict[str, int] = {}
    for case in cases:
        counts[case["name"]] = counts.get(case["name"], 0) + 1
    seen: dict[str, int] = {}
    for case in cases:
        base = case["name"]
        if counts[base] > 1:
            seen[base] = seen.get(base, 0) + 1
            case["name"] = f"{base}#{seen[base]}"
    return cases


def dump_module(module: str, cases: list[dict[str, Any]]) -> None:
    out = os.path.join(OUT_DIR, f"{module}.json")
    payload = {"module": module, "cases": cases}
    with open(out, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=2)
        f.write("\n")
    extracted = sum(1 for c in cases if "extract_error" not in c)
    print(f"{module}: {extracted}/{len(cases)} cases -> {out}")


def main() -> int:
    if not os.path.isdir(RS_TESTS):
        print(f"missing rust tests at {RS_TESTS}", file=sys.stderr)
        return 1
    for name in sorted(os.listdir(RS_TESTS)):
        if not name.endswith(".rs"):
            continue
        module = name[:-3]
        if module in SKIP_FILES:
            print(f"skip {module}")
            continue
        path = os.path.join(RS_TESTS, name)
        cases = extract_file(path, module)
        dump_module(module, cases)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
