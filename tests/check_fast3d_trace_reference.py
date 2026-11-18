#!/usr/bin/env python3
"""Compare the original trace fixture with selected, unmodified pinned Fast3D handlers.

The build runs outside the game. Only three methods are compiled, with the
minimal RDP fields they access. The checkout remains an external dependency.
"""

import argparse
from pathlib import Path
import shlex
import subprocess
from tempfile import TemporaryDirectory
from zipfile import ZipFile

from test_o2r_draw_trace import SCROLL, ROOT, fixture, trace

PIN = "da6daf1a50a5e46d6641408d52227e31e85e46df"
METHODS = ("GfxDpSetTile", "GfxDpSetTileSize", "GfxDpLoadTlut")
PRELUDE = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define G_TX_WRAP 0
#define G_TX_NOMASK 0
#define G_TX_CLAMP 2
#define G_IM_SIZ_16b 2
#define SUPPORT_CHECK(expression) assert(expression)

struct Tile {
    unsigned palette, fmt, siz, cms, cmt, masks, maskt, shifts, shiftt;
    unsigned line_size_bytes, tmem, tmem_index, uls, ult, lrs, lrt;
};

struct Rdp {
    Tile texture_tile[8]{};
    bool textures_changed[2]{};
    struct { unsigned siz; const uint8_t* addr; } texture_to_load{};
    uint8_t palette_staging[2][256]{};
    const uint8_t* palettes[2]{};
    const uint8_t* palette_dram_addr[2]{};
};

struct Interpreter {
    Rdp* mRdp;
    void GfxDpSetTile(uint8_t, uint32_t, uint32_t, uint32_t, uint8_t, uint32_t,
                      uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    void GfxDpSetTileSize(uint8_t, uint16_t, uint16_t, uint16_t, uint16_t);
    void GfxDpLoadTlut(uint8_t, uint32_t);
};
"""
DRIVER = r"""
int main() {
    Rdp rdp;
    Interpreter gfx{&rdp};
    uint8_t full[512], bank[32];
    std::fill_n(full, 512, 17);
    std::fill_n(bank, 32, 169);

    gfx.GfxDpSetTile(0, 2, 0, 256, 7, 0, 0, 2, 0, 0, 3, 0);
    rdp.texture_to_load = {2, full};
    gfx.GfxDpLoadTlut(7, 255);
    assert(rdp.palette_dram_addr[0] == full && rdp.palette_dram_addr[1] == full + 256);
    gfx.GfxDpSetTile(0, 2, 0, 304, 7, 0, 0, 2, 0, 0, 3, 0);
    rdp.texture_to_load = {2, bank};
    gfx.GfxDpLoadTlut(7, 15);
    assert(rdp.palette_dram_addr[0] == bank && rdp.palette_dram_addr[1] == full + 256);

    unsigned first = 256, replaced = 0;
    for (unsigned i = 0; i < 256; i++) {
        const unsigned firstByte = rdp.palettes[i / 128][2 * (i % 128)];
        const unsigned nextByte = rdp.palettes[i / 128][2 * (i % 128) + 1];
        assert(firstByte == nextByte);
        if (firstByte == 169) {
            first = std::min(first, i);
            replaced++;
        } else {
            assert(firstByte == 17);
        }
    }
    gfx.GfxDpSetTile(2, 0, 1, 0, 0, 3, 1, 2, 0, 2, 3, 1);
    gfx.GfxDpSetTile(2, 1, 1, 128, 1, 0, 0, 2, 0, 0, 3, 12);
    gfx.GfxDpSetTileSize(0, 4, 0, 32, 12);
    const Tile& a = rdp.texture_tile[0];
    const Tile& b = rdp.texture_tile[1];
    printf("%u %u %u %u %u %u %u %u %u %u %u %u %u\n",
           first, replaced, 256u, a.palette, a.line_size_bytes, a.cms, a.cmt,
           a.shifts, b.shifts, b.tmem, a.uls, a.lrs, a.lrt);
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("--cxx", default="clang++")
    args = parser.parse_args()
    pinned = subprocess.check_output(["git", "-C", args.checkout, "rev-parse", "HEAD"], text=True).strip()
    source = args.checkout / "src/fast/interpreter.cpp"
    dirty = subprocess.check_output(["git", "-C", args.checkout, "status", "--porcelain", "--",
                                     "src/fast/interpreter.cpp", "src/fast/LICENSE.txt"], text=True)
    if pinned != PIN or dirty:
        parser.error("reference checkout must contain the unmodified pinned Fast3D source")
    contents = source.read_text()
    methods = []
    for name in METHODS:
        start = contents.index(f"void Interpreter::{name}(")
        stop = contents.index("\n}", start) + 2
        methods.append(contents[start:stop])
    notice = (args.checkout / "src/fast/LICENSE.txt").read_text()
    with TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / "LICENSE.txt").write_text(notice)
        program = work / "reference.cpp"
        program.write_text(PRELUDE + "\n".join(methods) + DRIVER)
        target = work / "reference"
        subprocess.run(shlex.split(args.cxx) + ["-std=c++17", "-O1", "-fsanitize=address,undefined",
                                                "-fno-sanitize-recover=all", program, "-o", target], check=True)
        values = [int(x) for x in subprocess.check_output([target], text=True).split()]

        archive = work / "fixture.o2r"
        with ZipFile(archive, "w") as zipped:
            for path, data in fixture().items():
                zipped.writestr(path, data)
        report = trace.trace_archive(archive, ROOT, {8: trace.Reference(SCROLL)})
        ci4, ci8 = report["draws"][1]["textures"]
        a, b = ci4["descriptor"], ci8["descriptor"]
        observed = [
            ci4["palette"]["first_entry"], ci4["palette"]["loaded_entries"], ci8["palette"]["loaded_entries"],
            a["palette"], a["line_words"] * 8, a["s_mode"], a["t_mode"], a["s_shift"],
            b["s_shift"], b["tmem_words"], *(a["bounds_10_2"][i] for i in (0, 2, 3)),
        ]
        if values != observed:
            raise AssertionError(f"pinned Fast3D {values} differs from trace {observed}")
    print("Pinned Fast3D: tile descriptor, tile bounds, and CI4-over-CI8 TLUT parity under ASan/UBSan passed.")


if __name__ == "__main__":
    main()
