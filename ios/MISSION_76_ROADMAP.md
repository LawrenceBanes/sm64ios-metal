# OoT expansion roadmap — Mission 76

Scene identities follow the pinned [OoT scene table](https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/include/tables/scene_table.h). Counts below were measured from the supplied NTSC 1.0 archive. A generic audit without a reconstruction profile is deliberately strict: missing pass initialization remains a failure, rather than receiving assumed defaults. Existing Hyrule uses its validated daytime profile.

| Region | Resource stem | Rooms | Reachable DLs / textures / collision headers | Strict candidate draws / trace issues |
|---|---|---:|---|---|
| Hyrule Field | `spot00` | 1 | 37 / 51 / 1 | 844 / 0 with Hyrule profile |
| Market day | `market_day` | 1 | 18 / 0 / 1 | 257 / 0 |
| Kakariko Village | `spot01` | 1 | 22 / 38 / 1 | 413 / 4 |
| Kokiri Forest | `spot04` | 3 | 40 / 60 / 1 | 1054 / 6 |
| Lost Woods | `spot10` | 10 | 88 / 35 / 1 | 1181 / 3 |
| Lake Hylia | `spot06` | 1 | 18 / 43 / 1 | 512 / 3 |
| Gerudo Valley | `spot09` | 1 | 48 / 21 / 1 | 205 / 10 |
| Death Mountain Trail | `spot16` | 1 | 41 / 36 / 1 | 346 / 12 |
| Sacred Forest Meadow | `spot05` | 1 | 18 / 27 / 1 | 446 / 1 |

These are resource graph counts, including known setup variants; they do not imply that every reference is resolved during draw execution. In particular Market texture references are not established by the shallow dependency scan. All listed default room meshes and collision resources exist. None of the additional regions is exported as playable in this mission.

## Ranked next work

1. **Validate scaled Hyrule on iPhone.** Confirm fences, ground, entrances and camera visibility at 1.75x; keep native Mario and camera distances. Existing material approximations, omitted water boxes and default collision mappings remain explicit.
2. **Market daytime static slice.** Its strict trace has 257 candidate draws and no unresolved call issue, making it a small next target. Reconstruct proven pass/fog/texture setup, then map collision surfaces and safe entrances. Its zero textures in the shallow graph is incomplete evidence, not evidence that textures are absent. Background/camera behavior may require separate handling.
3. **Kokiri Forest.** Reuse the existing six-call runtime-segment investigation and reconstruct validated pass/material context. There are 1,054 candidate draws, 3 rooms, one scene-wide collision header and 60 reachable texture resources. Multiroom collision ownership, door actor semantics and spawn transitions still need explicit conversion policies.
4. **Kakariko and Lost Woods.** Establish each scene's draw configuration and dynamic segment bindings. Lost Woods additionally needs room ownership/adjacency for its many room links and revision-matched exits; visual room adjacency alone does not implement Zelda's maze rules.
5. **Lake Hylia and Gerudo Valley.** Resolve palette/tile and dynamic material commands, then decide explicit policies for water, bridges, hazards and actor-dependent scenery. The current runtime does not simulate Zelda water or events.
6. **Death Mountain / remaining overworld.** Branch-dependent lists and dynamic environment behavior need source-grounded interpretation. Preserve unsupported states visibly until implemented; do not borrow Hyrule pass defaults globally.

All region-to-region connections still require NTSC 1.0 global entrance mapping, spawn/setup policy, source exit indices and gameplay-compatible return routes. The inventory retains all 1,118 unresolved exit records and source metadata. No scripted or seamless connections are added here.

## Memory estimates and limits

The runtime never uploads the entire library. Its index is 1.55 MB, capped at 4 MiB; individual PNG reads remain capped at 256 KiB and dimensions are validated against the scene material. Region and collision limits remain those enforced by WSC and the expanded SM64 grid. Exact future GPU memory depends on palette variants, material crops, batching and active room ownership; source packed byte sizes are not GPU allocation measurements.

Reachable full-image RGBA estimates from the inventory: Hyrule 309,056 bytes; Kakariko 241,728; Kokiri 289,792; Sacred Meadow 161,792; Lake Hylia 184,640; Death Mountain Trail 288,832. These exclude missing/dynamic references, palette variants, geometry, collision, command storage, allocator overhead and Metal allocations. Market has no reliable texture-memory estimate yet. Each future exporter report must provide its own decoded texture and package sizes before device performance claims.
