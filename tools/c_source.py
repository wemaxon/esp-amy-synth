"""Small source-inspection helper for structural firmware contracts."""

import re


def function_body(source: str, name: str) -> str:
    # Ignore braces in comments and literals, but retain offsets into the source.
    masked = re.sub(
        r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
        lambda match: re.sub(r"[^\n]", " ", match.group()),
        source,
    )
    definition = re.search(
        rf"^[\w *\t]+\b{re.escape(name)}\s*\([^;{{}}]*\)\s*{{",
        masked, re.MULTILINE,
    )
    if definition is None:
        raise SystemExit(f"FAIL: missing function definition {name}")
    brace = definition.end() - 1
    depth = 0
    for index in range(brace, len(masked)):
        if masked[index] == "{":
            depth += 1
        elif masked[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise SystemExit(f"FAIL: unterminated function {name}")
