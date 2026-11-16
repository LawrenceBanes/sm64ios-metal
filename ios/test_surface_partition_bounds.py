#!/usr/bin/env python3
"""Compile-time/runtime checks for stock and opt-in OoT collision grids."""
from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SurfacePartitionBoundsTests(unittest.TestCase):
    def test_build_tracks_feature_mode_for_objects_that_embed_grid_dimensions(self):
        makefile = (ROOT / "Makefile.split").read_text(encoding="utf-8")
        main_makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        stamp = "$(BUILD_DIR)/.ios_world_mode"
        rule_start = makefile.index("$(BUILD_DIR)/src/engine/surface_load.o")
        rule_end = makefile.index("\n\n", rule_start)
        rule = makefile[rule_start:rule_end]
        self.assertIn("$(BUILD_DIR)/src/engine/surface_collision.o", rule)
        self.assertIn("$(BUILD_DIR)/src/game/area.o", rule)
        self.assertIn("$(BUILD_DIR)/src/game/mario.o", rule)
        self.assertIn(stamp, rule)
        self.assertIn("$(IOS_HYRULE_WORLD)", makefile)
        self.assertIn("$(IOS_CONNECTED_WORLD_TEST)", makefile)
        self.assertIn("IOS_CONNECTED_WORLD_TEST ?= 0", main_makefile)
        self.assertIn("$(BUILD_DIR)/levels/castle_grounds/leveldata.o", makefile)
        self.assertIn("$(BUILD_DIR)/include/level_headers.h: levels/level_headers.h.in $(BUILD_DIR)/.ios_world_mode",
                      main_makefile)

    def test_default_and_hyrule_grids_cover_their_coordinate_ranges(self):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        if not compiler or shutil.which(compiler[0]) is None:
            self.skipTest("host C compiler is unavailable")
        source = r'''#include <assert.h>
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
_Static_assert(sizeof(gStaticSurfacePartition) / sizeof(gStaticSurfacePartition[0])
               == SURFACE_PARTITION_DIM, "static partition width mismatch");
_Static_assert(sizeof(gDynamicSurfacePartition) / sizeof(gDynamicSurfacePartition[0])
               == SURFACE_PARTITION_DIM, "dynamic partition width mismatch");
int main(void) {
#if defined(IOS_HYRULE_WORLD) || defined(IOS_CONNECTED_WORLD_TEST)
    assert(LEVEL_BOUNDARY_MAX == 32768);
    assert(SURFACE_PARTITION_DIM == 64);
    assert(surface_partition_coord_in_bounds(-32768));
    assert(surface_partition_coord_in_bounds(32767));
    assert(!surface_partition_coord_in_bounds(32768));
    assert(surface_partition_cell_index(-32768) == 0);
    assert(surface_partition_cell_index(-32767) == 0);
    assert(surface_partition_cell_index(32767) == 63);
#else
    assert(LEVEL_BOUNDARY_MAX == 8192);
    assert(SURFACE_PARTITION_DIM == 16);
    assert(!surface_partition_coord_in_bounds(-8192));
    assert(surface_partition_coord_in_bounds(-8191));
    assert(surface_partition_coord_in_bounds(8191));
    assert(!surface_partition_coord_in_bounds(8192));
    assert(surface_partition_cell_index(-8191) == 0);
    assert(surface_partition_cell_index(8191) == 15);
#endif
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "partition_test.c"
            source_path.write_text(source, encoding="utf-8")
            modes = (("classic", ()),
                     ("hyrule", ("-DIOS_HYRULE_WORLD=1",)),
                     ("connected", ("-DIOS_CONNECTED_WORLD_TEST=1",)))
            for name, mode_flags in modes:
                binary = Path(directory) / name
                command = [*compiler, "-std=gnu11", "-D_LANGUAGE_C", "-DNON_MATCHING=1",
                           "-DAVOID_UB=1", f"-I{ROOT / 'include'}",
                           f"-I{ROOT / 'src'}", f"-I{ROOT}", *mode_flags]
                compiled = subprocess.run([*command, str(source_path), "-o", str(binary)],
                                          text=True, capture_output=True)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                result = subprocess.run([str(binary)], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_opt_in_bounds_cover_imported_field_and_connected_forest_branch(self):
        source = r'''#include <assert.h>
#include "engine/surface_collision.h"
int main(void) {
#if !defined(IOS_HYRULE_WORLD) && !defined(IOS_CONNECTED_WORLD_TEST)
    return 1;
#else
    const int coordinates[] = {-11100, -11048, -9200, 4200, 16529, 6732};
    unsigned int i;
    for (i = 0; i < sizeof(coordinates) / sizeof(coordinates[0]); ++i) {
        assert(coordinates[i] > -LEVEL_BOUNDARY_MAX);
        assert(coordinates[i] < LEVEL_BOUNDARY_MAX);
        assert(surface_partition_cell_index(coordinates[i]) >= 0);
        assert(surface_partition_cell_index(coordinates[i]) < SURFACE_PARTITION_DIM);
    }
    return 0;
#endif
}
'''
        compiler = shlex.split(os.environ.get("CC", "cc"))
        if not compiler or shutil.which(compiler[0]) is None:
            self.skipTest("host C compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "world_bounds_test.c"
            binary = Path(directory) / "world_bounds"
            source_path.write_text(source, encoding="utf-8")
            compiled = subprocess.run(
                [*compiler, "-std=gnu99", "-D_LANGUAGE_C", "-DNON_MATCHING=1",
                 "-DAVOID_UB=1", "-DIOS_CONNECTED_WORLD_TEST=1", f"-I{ROOT / 'include'}",
                 f"-I{ROOT / 'src'}", f"-I{ROOT}", str(source_path), "-o", str(binary)],
                text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
