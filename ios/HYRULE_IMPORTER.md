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
display-list texture references (including the palette). It decodes 1,639 triangles and 52 texture resources,
including the RGBA16 palette used by CI4. The source collision header contains
1,162 vertices and 1,579 triangles; all triangle winding/normals and bounds
were checked, and the selected spawn is over valid ground.

The O2R display lists are F3DEX2 streams with custom hash-wide resource
references. The importer resolves those hashes and rewrites hashed texture,
vertex, and nested-list commands into standard SM64 `G_SETTIMG`, `G_VTX`, and
`G_DL` commands. It retains supported RDP/GBI material, tile, combiner, filter,
and geometry-state commands for the existing graphics interpreter.
Embedded builds use raw texels. External-data builds put stable texture names
in the executable and package the corresponding decoded PNGs in `res/base.zip`.

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

## Mission 55.1 — Castle forest gateway

When `IOS_HYRULE_WORLD=1` is enabled, Castle Grounds gains a localized route
along its western wooded edge. It starts at `x=-7420`, joins original Castle
Grounds collision at `y=260`, and continues to `x=-11100` in a `z=720..2200`
passage. The authored SM64 trail mesh follows the same collision stations; an
open timber arch marks the entrance, and the existing Castle Grounds tree
model is reused along both sides. The default build has the original 490
collision vertices and no added gateway display list or warp.

The route uses the existing SM64 `Collision`, `Gfx`, `OBJECT`, and
`bhvFadingWarp` systems. Castle warp node `0x09` leads to Hyrule Field area 1
node `0x01`; the Hyrule return warp leads back to Castle node `0x09`. The
importer finds the Hyrule return warp and recovery-kart positions on nearly
flat collision, checks their height against the selected start, and converts
OoT's signed binary-angle yaw to the degrees expected by SM64's `MARIO_POS`.
The Hyrule default spawn remains available when entering the level through the
existing debug path.

Kart object ownership is area-local in the current engine. A mounted kart is
not carried through the level load with its momentum and mount state; the
target level places a new recoverable kart on validated terrain. Luigi's
selection and Reality Debugger state are process-global in the current code,
so this gateway does not reinitialize them. The passage reuses existing bird
chirps and tree models; level music changes only when the normal warp loads
Hyrule, since the current audio/lighting path has no spatial cross-fade.

The gateway has collision-continuity regressions and opt-in host C syntax
checks. There is still no Hyrule-enabled iOS build or iPhone test. The full IPA
workflow accepts a separate optional private draft release tag for the
supplied `oot.o2r`; see `KOKIRI_FOREST_AUDIT.md` for the input convention and
the Kokiri follow-up plan. The archive itself is not placed in Actions
artifacts or IPA release assets; only generated game output is compiled into
the private IPA.

## Mission 58 — Hyrule Field gate status

The brown structure is part of the imported `spot00` room geometry and
collision. The O2R scene provides exit IDs and start positions but the current
lowering emits only the static room and collision; it does not import OOT
transition actors or gate behavior. The generated gate-area proximity anchor
uses supported Hyrule start-position entry 4 (`[-5891, -881, 15676]` in the
supplied archive), which is on the nearby collision floor and within the
brown structure's render-vertex bounds. This anchor is geometric evidence; it
does not identify the gate's intended destination.

The Hyrule level keeps its existing, separate `bhvFadingWarp` back to Castle
Grounds. No forward destination is declared by the gate or registered as an
SM64 level. Kokiri Forest is listed as a future planned region, but that alone
does not establish it as this gate's destination. An iOS Hyrule diagnostic
build can opt in with `IOS_HYRULE_GATE_DIAGNOSTICS=1`; the Makefile propagates
this as a compile define, and the workflow enables it only when an O2R release
tag is supplied. The probe runs after `update_mario_inputs()` refreshes
wall/floor collision, and ignores frames with no valid floor. Entering the
gate-area radius emits a bounded position/floor/proximity diagnostic and a
brief native status message that the destination is not configured. It does
not enqueue an invalid warp or invent a destination. A forward transition
remains blocked until its intended destination and entrance are specified and
registered; the proximity feedback itself remains device-unverified until this
diagnostic IPA is tested.

## Mission 61 audit handoff

`IOS_CONNECTED_WORLD=1` mounts the supported field in Castle Grounds area 1.
The Castle terrain command stream keeps its original records and appends two
vertex banks for the new walkway and field. The ceiling and wall checks use
the same static collision grid as Castle. The field keeps its existing opaque
and translucent display lists and texture buffers. Castle's GeoLayout applies
the same placement as the collision generator: source `(x, y, z)` maps to
`(-13200 - z, 260 + y, 1460 + x)`. Castle retains its free roam camera and
20,000-unit far plane.

Host movement exposed two obstacles: the original west ridge reaches
`y=1729` above the legacy trail, and the field entrance crosses a 140-unit
drop. An original raised walkway crosses the ridge and descends along the
trail into the field opening. It bridges the drop without changing stock
Castle triangles. Connected mode suppresses the legacy Hyrule warp pair.
The standalone level remains registered, and normal Castle warps are unchanged.

The host harness first crossed the inlet using 130 representative field
triangles, then passed in three lanes in both directions with the full import
and the engine's walking speed, floor, wall and ceiling queries. The combined
real import used 2,510 static surfaces and 7,370 nodes, with zero rejected
surfaces. With the separate legacy forest test also enabled, it used 2,540
surfaces and 7,534 nodes. The expanded pools already used by ordinary Hyrule
builds hold 8,192 surfaces and 32,768 nodes. The extra transformed collision
stream uses about 16 KiB of compiled data. The existing 37 field render lists
run through Castle's scene graph when enabled, so Metal frame pacing, texture
residency and far views need device measurement. The importer still omits
three unsupported segmented display list calls and does not import OoT actors
or water behavior.

On device, install the local build from [BUILDING.md](BUILDING.md) and:

1. Enter Castle Grounds. Look for the area collision log with `rejected=0`.
   Head west from the grove onto the raised walkway near `(-4650,260,1460)`.
2. Walk over the crest, descend through the field opening, continue past
   `x=-14600`, then walk back along the same route. Check that there is no
   fade or transition and both sides remain in Castle Grounds area 1.
3. Check camera turns, wall clearance, deck edges, texture rendering and frame
   pacing at the crest, the narrow opening, and deep in the field. Re-enter
   through a stock Castle door to check area reload.
4. Near `(-28876,-621,-4431)`, check for the gate probe and unavailable message
   from Castle area 1. With hats enabled, throw and bounce on each side.
   Compare with a separate connected-off Hyrule build to verify the warp pair.

The host tests exercise compiled geometry wiring and collision math without
launching UIKit, Metal, scene objects or an iPhone game session.

### Audit corrections

The workflow's `connected_world` input is off by default. The opted build
requires a private Hyrule tag and passes the flag to both Xcode's environment
and its command line settings. The local runner regression follows the
setting through the Xcode shell phase and real Make flags into the compiled
Castle geometry. See [CI inputs](GITHUB_ACTIONS_CI.md#full-game-build-private-inputs-required).

With the actual import, the host harness also walked next to both crest edges,
pushed against each crest and inlet rail, and jumped across the crest in both
directions on both sides. Each jump landed back on the deck and resumed
walking. These checks cover accidental exits from the tested paths. The low
rails are not intended to contain a deliberate jump over the side.

The three omitted calls depend on runtime segments set by
[Shipwright's Hyrule Field draw configuration](https://github.com/HarbourMasters/Shipwright/blob/develop/soh/src/code/z_scene_table.c#L940-L979).
The archive and render manifest identify their callers:

| Source display list suffix | Segment | Effect currently omitted |
| --- | --- | --- |
| `014E28` | `0x08` | Animated two-tile scrolling. The caller's 46 translucent triangles remain in the render output. |
| `015100` | `0x09` | A faster two-tile scroll. The caller's six translucent triangles remain. |
| `012C38` | `0x0A` | A six-triangle overlay from `012B20`, faded in outside daytime. The daytime branch of the source effect draws nothing. |

The first two calls affect visible material animation and can leave different
tile offsets. The last affects a small time-dependent visual detail. These
calls do not load actors or supply terrain collision. The importer retains the
no-ops and the original collision alignment. Check the translucent materials
and the area around the overlay on device if visual fidelity matters.

## Mission 62 rendering handoff

Run 44224738009 shipped commit `b859ba02`. Its arm64 executable matches the
private import across 39 display lists (3,708 commands), 37 vertex arrays
(2,971 vertices), and all 51 referenced texture payloads. Both connected
GeoLayout nodes contain the expected quarter-turn and translation.
The opaque and translucent lists are also used by the standalone level.

- **Position.** A type-2 room entry holds a bounding sphere. Its display
  lists already contain room-space coordinates. Adding the center moved
  different terrain patches by different amounts, extending maximum drawn Z
  from 16,529 to 31,526. With source coordinates, 833 distinct render positions
  exactly coincide with collision vertices. The shipped geometry had zero.
  [Original room drawing code](https://github.com/zeldaret/oot/blob/main/src/code/z_room.c#L117-L183)
- **Checkerboard.** The shipping external-data renderer interpreted texel
  bytes as NUL-terminated names. None of the 51 computed resource paths
  existed. Host replay of both placements with the shipping pack uploaded
  the fallback on all 39 uploads. The corrected build path makes an explicit
  PNG list for Make, including the decoded paletted image. External intensity
  textures carry intensity in the alpha channel.
  [Nintendo texture format reference](https://ultra64.ca/files/documentation/online-manuals/man/pro-man/pro12/12-04.html)
- **Materials.** Both field passes left fog active and overwrote the layer
  blend/depth mode. They now establish neutral static lighting, suppress
  inherited fog, and finish in the corresponding SM64 layer mode. The
  two water lists use 52 triangles. Their two-cycle alpha expression reduces
  to primitive alpha. The importer places that result in cycle one so
  the existing interpreter sends the source opacity (102/255) to the backend.
  Its single-cycle RGB approximation and the three segmented effects described
  above still limit visual fidelity.

Run `CC=clang tests/run_linux_checks.sh` for the synthetic render regression.
For the private comparison, import the authorized archive with
`--connected-world` and run the suite with `HYRULE_GENERATED_RENDER_DIR`
set to the resulting absolute `render` directory. The Linux replay mounts a
real Make-built base pack, decodes its PNGs with the game loader, and checks
the stream at the renderer API boundary. Metal rasterization, camera clipping
at the crest, translucent ordering and the source of the blue pixels in the
iPhone recording still require a device comparison. Use the same camera route
in Normal, Flat and UV modes in standalone and connected Hyrule. Inspect Castle
water, Mario and his cap immediately before and after the return crossing.
