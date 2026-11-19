#!/usr/bin/env python3
"""Compare pass setup and generated packets against the two pinned source checkouts."""

import argparse
from pathlib import Path
import shlex
import subprocess
from tempfile import TemporaryDirectory

from test_o2r_draw_trace import trace
from check_fast3d_trace_reference import PIN
from tools import fast3d_pass_state as setup


OOT_PIN = "14759abf9abb4295de142cdc1452998630ea70fb"
PRELUDE = r"""
#include <cstdio>
#define F3DEX_GBI_2
#include "libultraship/libultra/gbi.h"
struct GraphicsContext { Gfx commands[5]; };
#define GRAPH_ALLOC(ctx, bytes) ((ctx)->commands)
static const Gfx room_setup[] = {
"""
DRIVER = r"""
static void print(const Gfx* packets, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
        std::printf("%08x %08x\n", (u32)packets[i].words.w0, (u32)packets[i].words.w1);
}
int main() {
    print(room_setup, sizeof(room_setup) / sizeof(room_setup[0]));
    GraphicsContext state{};
    print(Gfx_TwoTexScroll(&state, 0, 90, 114, 32, 32, 1, 37, 114, 32, 32), 5);
    print(Gfx_TwoTexScroll(&state, 0, 0, (s16)-3, 32, 16, 1, 0, (s16)-3, 32, 16), 5);
}
"""


def source_at(checkout, path, pin):
    sha = subprocess.check_output(["git", "-C", checkout, "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", checkout, "status", "--porcelain", "--", path], text=True)
    if sha != pin or dirty:
        raise ValueError(f"{checkout} must contain unmodified {path} at {pin}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("oot", type=Path)
    parser.add_argument("libultraship", type=Path)
    parser.add_argument("--cxx", default="clang++")
    args = parser.parse_args()
    for checkout, path, pin in (
            (args.oot, "src/code/z_rcp.c", OOT_PIN),
            (args.libultraship, "include/libultraship/libultra", PIN)):
        source_at(checkout, path, pin)
    source = (args.oot / "src/code/z_rcp.c").read_text()
    section = source.index("/* SETUPDL_25 */")
    setup_macros = source[section:source.index("\n    },", section)]
    start = source.index("Gfx* Gfx_TwoTexScroll(")
    scroll_method = source[start:source.index("\n}", start) + 2]
    with TemporaryDirectory() as tmp:
        work = Path(tmp)
        program = work / "reference.cpp"
        program.write_text(PRELUDE + setup_macros + "\n};\n" + scroll_method + DRIVER)
        target = work / "reference"
        subprocess.run(shlex.split(args.cxx) + ["-std=c++17", "-O1", "-fsanitize=address,undefined",
                                                "-fno-sanitize-recover=all", "-I", args.libultraship / "include",
                                                program, "-o", target], check=True)
        observed = [tuple(int(word, 16) for word in line.split())
                    for line in subprocess.check_output([target], text=True).splitlines()]
    context = setup.KokiriInputs(37, 6, True, 150)
    proposed = (setup.ROOM_SETUP_25.packets + context.prelude("translucent", 0).bindings[8].packets
                + context.prelude("opaque", 0).bindings[12].packets)
    if observed != list(proposed):
        raise AssertionError(f"pinned GBI expanded as {observed} and trace generated {proposed}")
    print("Pinned SETUPDL_25 macro words and dual-tile scroll commands match under ASan/UBSan.")


if __name__ == "__main__":
    main()
