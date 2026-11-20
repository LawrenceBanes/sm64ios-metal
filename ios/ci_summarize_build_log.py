#!/usr/bin/env python3
"""Emit bounded, content-free compiler failure categories from a private Xcode log."""

import argparse
from pathlib import Path
import re


SOURCE_ERROR = re.compile(r"(?:^|\s)([^\s:]+\.(?:c|cc|cpp|h|m|mm)):(\d+)(?::\d+)?:\s*(?:fatal )?error:")
DIAGNOSTIC_CATEGORIES = (
    (re.compile(r"file not found|no such file", re.I), "missing include"),
    (re.compile(r"use of undeclared|undeclared identifier|undeclared (?:variable|function|symbol)|unknown type name", re.I), "undeclared symbol or type"),
    (re.compile(r"redefinition|redefined|duplicate (?:symbol|definition|member)|already defined", re.I), "duplicate or repeated declaration"),
    (re.compile(r"static declaration.*(?:follows|conflicts)|conflicting (?:types|linkage)|different kind of symbol", re.I), "declaration linkage mismatch"),
    (re.compile(r"incompatible|no matching|cannot initialize|cannot convert|conflicting types|invalid operands|argument of type", re.I), "type or call mismatch"),
    (re.compile(r"expected (?:identifier|';'|\)|\(|expression|declaration|specifier|type)", re.I), "syntax or declaration error"),
    (re.compile(r"no member named|member reference|has no member", re.I), "structure member mismatch"),
)


def summarize(text: str) -> list[str]:
    summary = ["Full iPhoneOS game build: FAILED", "Raw Xcode output remains runner-local and is not published."]
    for line in text.splitlines():
        match = SOURCE_ERROR.search(line)
        if match:
            filename = Path(match.group(1)).name
            # Only a basename, line number, and allowlisted category leave the runner.
            kind = next((label for pattern, label in DIAGNOSTIC_CATEGORIES if pattern.search(line)),
                        "compiler error")
            summary.append(f"{filename}:{match.group(2)}: {kind}")
        elif "Undefined symbols for architecture" in line:
            summary.append("arm64 linker: undefined symbols")
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
