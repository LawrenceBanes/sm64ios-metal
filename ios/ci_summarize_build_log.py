#!/usr/bin/env python3
"""Emit bounded, content-free compiler failure categories from a private Xcode log."""

import argparse
from pathlib import Path
import re


SOURCE_ERROR = re.compile(r"(?:^|\s)([^\s:]+\.(?:c|cc|cpp|h|m|mm)):(\d+)(?::\d+)?:\s*(?:fatal )?error:")
LINKER_SYMBOL = re.compile(r'^\s+["\']([A-Za-z0-9_.$@?]+)["\'],\s+referenced from:')
COMPILE_ACTION = re.compile(r"\b(?:CompileC|CompileC\+\+|CompileObjectiveC|CompileObjectiveC\+\+)\b")
SOURCE_BASENAME = re.compile(r"(?:^|[/\\\s])([^/\\\s]+\.(?:c|cc|cpp|m|mm))(?=[:\s]|$)")


def summarize(text: str) -> list[str]:
    summary = ["Full iPhoneOS game build: FAILED", "Raw Xcode output remains runner-local and is not published."]
    compile_unit = None
    for line in text.splitlines():
        if COMPILE_ACTION.search(line):
            unit_matches = list(SOURCE_BASENAME.finditer(line))
            if unit_matches:
                compile_unit = unit_matches[-1].group(1)
        match = SOURCE_ERROR.search(line)
        linker_symbol = LINKER_SYMBOL.match(line)
        if match:
            filename = Path(match.group(1)).name
            # Only the file basename, line number, and broad class leave the runner.
            kind = "compiler error"
            lowered = line.lower()
            if "file not found" in lowered:
                kind = "missing include"
            elif "undeclared" in lowered:
                kind = "undeclared symbol"
            elif "incompatible" in lowered or "no matching" in lowered:
                kind = "type or call mismatch"
            elif "too many arguments" in lowered or "too few arguments" in lowered:
                kind = "argument count mismatch"
            elif "expected" in lowered:
                kind = "syntax or declaration error"
            elif "redefinition" in lowered or "duplicate" in lowered:
                kind = "duplicate definition"
            elif "not an integer constant" in lowered:
                kind = "invalid constant expression"
            elif "array designator" in lowered or "exceeds array bounds" in lowered:
                kind = "array bounds error"
            elif "use of undeclared" in lowered:
                kind = "undeclared symbol"
            elif "unknown type name" in lowered:
                kind = "unknown type"
            elif "macro" in lowered:
                kind = "macro expansion error"
            unit = f" (while compiling {compile_unit})" if compile_unit else ""
            summary.append(f"{filename}:{match.group(2)}: {kind}{unit}")
        elif "Undefined symbols for architecture" in line:
            summary.append("arm64 linker: undefined symbols")
        elif linker_symbol:
            summary.append(f"arm64 linker missing: {linker_symbol.group(1)}")
        elif "ld: " in line and "error:" in line:
            summary.append("arm64 linker: error")
        if len(summary) >= 14:
            break
    if len(summary) == 2:
        summary.append("No compiler diagnostic was safely classifiable; inspect the runner-local build stage.")
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    print("\n".join(summarize(args.log.read_text(errors="replace"))))


if __name__ == "__main__":
    main()
