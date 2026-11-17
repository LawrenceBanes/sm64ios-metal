#!/usr/bin/env python3
"""Emit bounded, content-free compiler failure categories from a private Xcode log."""

import argparse
from pathlib import Path
import re


SOURCE_ERROR = re.compile(r"(?:^|\s)([^\s:]+\.(?:c|cc|cpp|h|m|mm)):(\d+)(?::\d+)?:\s*(?:fatal )?error:")


def summarize(text: str) -> list[str]:
    summary = ["Full iPhoneOS game build: FAILED", "Raw Xcode output remains runner-local and is not published."]
    for line in text.splitlines():
        match = SOURCE_ERROR.search(line)
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
