# Mission 67B progress

Branch `mission-67-native-scene-foundation`, starting HEAD
`248f75c842373ce0075b1b5b5fdede0aa59db3d2`.

## Archive inventory

`python3 tools/o2r_inventory.py /home/larry/Downloads/oot.o2r --verify-crc --output build/mission67b/o2r-inventory.json`
passed. The report stays in the ignored build directory. It lists 38,390
entries, 101 scene roots, 388 room headers, and 385 alternate headers. It
records 635 resolved room links, three unresolved room links, and 1,118
cross-scene exit records that require a global entrance mapping. Parser
statuses are 24,979 parsed, 5,016 partial, and 8,395 unsupported. These are
metadata results, not successful geometry imports. Kokiri Forest
(`spot04_scene`) has three rooms and 12 entrances. Hyrule Field
(`spot00_scene`) has one room and 18 entrances. The catalog found host
dependencies for both, including 60 and 51 texture records respectively.

## Implementation checkpoint

Next, define a bounded serialized package for original scene content and
connect its provider to the existing Gfx and collision systems. Validate with
authored fixtures and stock area load/unload hooks. The 67 alpha fix is
already in the parent commit. No game levels or assets were exported from
the archive. No CI run, IPA, release, or iPhone validation has occurred.

## WSC1 provider and stock area integration

- `tools/world_scene_package.py` writes deterministic WSC1 packages from
  authored JSON. Source files, generated packages, and PNGs can stay local.
- `src/engine/world_scene_file.c` reads at most 4 MiB per package, validates
  records, builds trusted Gfx commands, and owns room buffers. It attaches
  collision through `world_region`. The package manager owns residency rather
  than the older region distance updater. The texture loader bounds both
  file size and decoded dimensions. Package release invalidates texture-name
  references before freeing memory. Metal still manages its own GPU copies.
- `src/game/area.c` opens the default room after stock terrain loading and
  closes the package on area teardown. `src/game/rendering_graph_node.c`
  submits room display lists in the opaque camera pass. The display-list
  interpreter restores its RDP, geometry, and vertex state after each room.
- `Makefile.split` tracks changes to the scene runtime flag and external-data
  mode for the affected objects in incremental builds.
- Live gameplay keeps the default room attached until area teardown. Scene
  and room switching are exercised by the host provider replay. Gameplay
  needs a destination and spawn adapter that also updates references held by
  Mario, the camera, and objects before live collision can be removed.
- `tests/test_world_scene_package.py` generates original fixtures and calls the
  real C provider, collision queries, Gfx interpreter, and area hooks. Stock
  area services are stubs in that harness, with no Apple renderer involved.

The supplied archive's SHA-256 is
`e3bf3ae3565768998026e60113ed3fc506fc5415e23299de41b9b42a5fbc3807`.
The default Hyrule room has three cataloged display lists requiring runtime
segment bindings. Kokiri's default rooms have four in their scene directory.
This is a concrete parser limitation. The inventory does not establish that
those references, the alpha combiner, or any other single factor caused the
device's invisible terrain. The prior RDP constant-alpha-one fix remains
in the branch and passes its existing host replay.

## Validation checkpoint

The authored package round trip and the stock area hook replay have passed.
Final validation runs the Linux suite, a sanitized package replay, and
compiles the provider with the feature and external-data combinations.
Apple tools are unavailable locally. The real scene integration is incomplete,
so the workflow has not been dispatched.

## Local use with an original authored scene

Use `IOS_O2R_SCENE_RUNTIME=1` in the iOS build. With the app stopped, place the
package at `Documents/scenes/<id>.wsc`, where `id` is decimal
`level_id * 256 + area_index`. The package must include entrance 0 selecting
the initial room. Export it with:

```sh
python3 tools/world_scene_package.py authored-scene.json path/to/scenes/513.wsc
```

The current provider supports one optional opaque PNG per room with
`EXTERNAL_DATA=1`. A material such as `scenes/example.rgba16` resolves to
`Documents/gfx/scenes/example.rgba16.png`. The package stores signed 16-bit
area-local positions, signed 16-bit UVs in units of 1/32 texel, and RGBA vertex
colors. Both geometry and collision must use the same coordinate transform
before export. The active collision grid still determines the valid X/Z
range. When the package is present, the room is drawn alongside the area's
existing geometry and the normal Mario spawn remains in use.

## End of session

Verified implementation HEAD on `mission-67-native-scene-foundation` is
`554b6b8b9142cedaf93ab0565300c3cf4a38bc1b`. The follow-up commit records
this handoff only.

| Check | Result |
| --- | --- |
| Real archive catalog with ZIP CRC verification | Pass. Metadata only. |
| `CC=clang sh tests/run_linux_checks.sh` | Pass. Raw output in `build/mission67b/linux-checks.log`. |
| `CC=clang python3 tests/test_world_scene_package.py` | Pass. Two tests. Expected diagnostics for a truncated package and degenerate collision. |
| Package tests with `CC='clang -fsanitize=address,undefined -fno-sanitize-recover=undefined'` and `ASAN_OPTIONS=detect_leaks=0` | Pass. Raw output in `build/mission67b/scene-sanitizers.log`. Leak detection was disabled. |
| Strict provider syntax checks | Pass with the runtime off, on without external textures, and on with external textures. |
| GNU Make mode stamp | Pass. Switching 0 → 1 → 0 updated the stamp each time. |
| Apple SDK, Metal compiler, and iPhone runtime | Unverified. `xcodebuild`, `xcrun`, and `metal` are absent locally. |
| CI, IPA, or release | No run or artifact. No push or asset upload. |

The next reusable step is a standalone SM64 area that selects an original
authored package, verifies safe spawn collision, and refreshes cached surface
references before any live unload. A real OoT scene export, the Kokiri
tree and return warps, and hardware confirmation of the Hyrule rendering
issue remain outside the verified result.
