"""Conservative AiScript minifier: comments + whitespace outside strings/templates."""

from __future__ import annotations

# States
CODE = "CODE"
STR_DQ = "STR_DQ"  # "..."
STR_SQ = "STR_SQ"  # '...'
TEMPLATE = "TEMPLATE"  # `...`


def minify_ais(source: str) -> str:
    """Strip // and /* */ comments; collapse runs of whitespace in CODE to a single space.

    Strings (single/double) and template literals are copied verbatim (except that
    comments inside CODE are removed). Template handling follows backtick boundaries;
    `\\{` and nested `{` … `}` regions are preserved without whitespace collapse inside
    the template text parts; expression interiors use CODE-like stripping is not applied
    (expressions are copied character-by-character until matching `}` at depth 0).
    """
    out: list[str] = []
    i = 0
    n = len(source)
    state = CODE
    last_was_space = False

    def emit_space() -> None:
        nonlocal last_was_space
        if not out:
            return
        if last_was_space:
            return
        out.append(" ")
        last_was_space = True

    def emit_raw(ch: str) -> None:
        nonlocal last_was_space
        out.append(ch)
        last_was_space = ch.isspace()

    while i < n:
        c = source[i]

        if state == CODE:
            # Line comment
            if c == "/" and i + 1 < n and source[i + 1] == "/":
                i += 2
                while i < n and source[i] not in "\r\n":
                    i += 1
                continue
            # Block comment
            if c == "/" and i + 1 < n and source[i + 1] == "*":
                i += 2
                while i < n:
                    if i + 1 < n and source[i] == "*" and source[i + 1] == "/":
                        i += 2
                        break
                    i += 1
                continue
            # String start
            if c == '"':
                state = STR_DQ
                emit_raw(c)
                i += 1
                continue
            if c == "'":
                state = STR_SQ
                emit_raw(c)
                i += 1
                continue
            # Template start
            if c == "`":
                state = TEMPLATE
                emit_raw(c)
                i += 1
                continue
            # Whitespace collapse
            if c in " \t\r\n":
                j = i
                while j < n and source[j] in " \t\r\n":
                    j += 1
                i = j
                emit_space()
                continue
            emit_raw(c)
            i += 1
            continue

        if state == STR_DQ:
            emit_raw(c)
            if c == "\\" and i + 1 < n:
                emit_raw(source[i + 1])
                i += 2
                continue
            if c == '"':
                state = CODE
            i += 1
            continue

        if state == STR_SQ:
            emit_raw(c)
            if c == "\\" and i + 1 < n:
                emit_raw(source[i + 1])
                i += 2
                continue
            if c == "'":
                state = CODE
            i += 1
            continue

        if state == TEMPLATE:
            # Inside `...`
            if c == "\\":
                emit_raw(c)
                if i + 1 < n:
                    emit_raw(source[i + 1])
                    i += 2
                else:
                    i += 1
                continue
            if c == "`":
                emit_raw(c)
                state = CODE
                i += 1
                continue
            if c == "{":
                emit_raw(c)
                i += 1
                depth = 0
                while i < n:
                    cc = source[i]
                    if cc == '"' or cc == "'":
                        q = cc
                        emit_raw(cc)
                        i += 1
                        while i < n:
                            z = source[i]
                            emit_raw(z)
                            if z == "\\" and i + 1 < n:
                                emit_raw(source[i + 1])
                                i += 2
                                continue
                            i += 1
                            if z == q:
                                break
                        continue
                    if cc == "`":
                        raise ValueError("nested template backtick inside expression (unsupported for minify)")
                    if cc == "{":
                        depth += 1
                        emit_raw(cc)
                        i += 1
                        continue
                    if cc == "}":
                        if depth == 0:
                            emit_raw(cc)
                            i += 1
                            break
                        depth -= 1
                        emit_raw(cc)
                        i += 1
                        continue
                    emit_raw(cc)
                    i += 1
                continue
            emit_raw(c)
            i += 1
            continue

    return "".join(out).strip()
