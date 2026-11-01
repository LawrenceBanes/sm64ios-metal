# Hyrule Field import: SM64 runtime integration

## Current implementation

The host importer reads Larry's user-supplied `oot.o2r` ZIP archive and lowers
the default `spot00` Hyrule Field room into the project's existing SM64 level
ABI. It does **not** create a second player controller or Hyrule-only movement
simulation.

The generated level uses the stock `MARIO` object, `lvl_init_or_update` loop,
camera and object systems. Its converted OoT collision mesh is passed to the
normal `TERRAIN(...)` level command, so Mario's existing floor, wall, ledge,
gravity, jump, and collision update code remains authoritative. The imported
static world geometry is submitted as ordinary F3DEX2 `Gfx` commands, which
continue through `gfx_pc.c` and the selected rendering backend, including
Metal. Hyrule is registered as an optional level only when
`IOS_HYRULE_WORLD=1`; ordinary builds do not add a level entry or shift the
existing level/save tables.

Run a local asset build with the supplied archive:

```sh
HYRULE_O2R_PATH=/path/to/oot.o2r ./build_ios.sh
```

The importer and generated files stay in the local ignored `build/` tree. Do
not add the archive, generated C, decoded texture files, or other archive data
to Git or public build artifacts. The optional level is selected through the
existing level-select/debug path; it does not replace Peach's Castle or alter
the default game entry.

## Decoded room and current coverage

For the supplied archive (38,390 ZIP entries; SHA-256
`e3bf3ae3565768998026e60113ed3fc506fc5415e23299de41b9b42a5fbc3807`), the
canonical `scenes/shared/spot00_scene/spot00_scene` points to one default
room. The importer resolves its 37 display lists, 37 vertex arrays, and 51
sampled texture images. It decodes 1,639 triangles and 52 texture resources,
including the RGBA16 palette used by CI4. The source collision header contains
1,162 vertices and 1,579 triangles; all triangle winding/normals and bounds
were checked, and the selected spawn is over valid ground.

The O2R display lists are F3DEX2 streams with custom hash-wide resource
references. The importer resolves those hashes and rewrites hashed texture,
vertex, and nested-list commands into standard SM64 `G_SETTIMG`, `G_VTX`, and
`G_DL` commands. It retains supported RDP/GBI material, tile, combiner, filter,
and geometry-state commands for the existing graphics interpreter. Raw
archived texel bytes are kept in the generated level data so the normal SM64
texture loader performs sampling and format conversion.

There are three ordinary segmented `G_DL` pointers in the selected room whose
segment bases are not represented in the O2R scene resource graph. The
importer replaces those three calls with `G_SPNOOP` and reports them; it does
not guess pointers or dereference them. Other scene headers, alternate rooms,
actors, object banks, and moving geometry are not included in this default-room
pass.

Collision triangles currently use `SURFACE_DEFAULT`. Collision geometry is
real and enters Mario's normal terrain queries, but OoT-specific surface
effects, water boxes, scene actors, transitions, and moving platforms are not
yet mapped. Coordinate axes are both Y-up and the default scale is 1.0; scale
remains provisional until the imported scene is inspected in-game. Geometry,
collision, and spawn coordinates use the same configured scale, and the tool
rejects values that cannot fit SM64's `s16` coordinate fields.

## Validation and limitations

Host regressions cover O2R headers, F3DEX2 command widths and references,
texture formats and palette loading, TMEM texture selection, native Gfx and
Collision lowering, coordinate scaling, spawn bounds, and use of the existing
Mario/TERRAIN/level update path. The exporter also syntax-checks generated C
with the host compiler when available.

This validates resource decoding and source generation, not full Xcode
compilation or visible iPhone rendering. The three omitted display-list calls,
provisional scale, generic collision surface type, and lack of alternate-room
or actor import remain visible limitations. The next Hyrule milestone is to
build this opt-in level from the private archive, inspect authentic geometry
and textures on device, then tune the shared SM64 collision integration based
on the actual playtest. Region expansion and seamless streaming are deferred.
