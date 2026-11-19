#!/usr/bin/env python3
"""Convert a supported, authorized O2R scene into a local Documents import tree."""

import argparse
from collections import defaultdict
from fractions import Fraction
from hashlib import sha256
import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
from zipfile import BadZipFile, ZipFile

if __package__:
    from . import hyrule_importer as o2r, o2r_draw_trace as trace, world_scene_package as wsc
else:
    import hyrule_importer as o2r
    import o2r_draw_trace as trace
    import world_scene_package as wsc

SCENE_ID = 4100
MAX_LAYOUT_BYTES = 1024 * 1024
TEXTURE_FORMATS = {(0, 2): 2, (0, 3): 1, (2, 0): 3, (2, 1): 4,
                   (3, 0): 7, (3, 1): 8, (3, 2): 9, (4, 0): 5, (4, 1): 6}
WHITE = [255] * 4


def commands_of(commands, opcode):
    return [command for command in commands if command["id"] == opcode]


def command_of(commands, opcode, label):
    entries = commands_of(commands, opcode)
    if len(entries) > 1:
        raise ValueError(f"{label}: duplicate scene command {opcode}")
    return entries[0] if entries else {}


def multiply_factors(expression):
    if isinstance(expression, tuple) and expression[0] == "multiply":
        return multiply_factors(expression[1]) + multiply_factors(expression[2])
    return [] if expression == 1 else [expression]


def combiner_channel(expression, channel, inputs, colors, opaque_alpha=False):
    if opaque_alpha:
        return False, 255, 255, None
    primitive = environment = 255
    shade = primitive_used = environment_used = False
    reverse = None
    for term in multiply_factors(expression):
        if term == f"shade.{channel}" and not shade:
            shade = True
        elif term == f"primitive.{channel}" and not primitive_used:
            primitive_used = True
            primitive = colors["primitive"]["rgba"][3] if channel == "a" else colors["primitive"]["rgba"][:3]
        elif term == "environment.rgb" and channel == "rgb" and not environment_used:
            environment_used = True
            environment = colors["environment"]["rgba"][:3]
        elif term == 0 and not inputs:
            primitive = 0
        elif len(inputs) == 1 and term == f"tex{inputs[0]}.{channel}" and reverse is None:
            reverse = False
        elif len(inputs) == 2 and isinstance(term, tuple) and len(term) == 5 and reverse is None:
            op, a, b, factor, d = term
            sample_pair = {f"tex0.{channel}", f"tex1.{channel}"}
            if op != "combine" or b != d or {a, b} != sample_pair or factor != "environment.a":
                raise ValueError(f"unsupported {channel} dual-texture combiner {term!r}")
            reverse = a == f"tex0.{channel}"
        else:
            raise ValueError(f"unsupported {channel} combiner factor {term!r}")
    if inputs and reverse is None:
        raise ValueError(f"{channel} channel ignores a texture sampled by the other channel")
    return shade, primitive, environment, reverse


def palette_bytes(resources, entries, palette):
    payload = bytearray()
    for i in range(palette["first_entry"], palette["first_entry"] + palette["entry_count"]):
        if i not in entries:
            raise ValueError(f"missing physical TLUT entry {i}")
        path, offset, first, _, _ = entries[i]
        _, version, endian, body = resources.typed_body(path, ("Texture",))
        descriptor = o2r.parse_texture_descriptor(body, endian, version, path)
        if (descriptor["flags"] or descriptor["horizontal_byte_scale"] != 1
                or descriptor["vertical_pixel_scale"] != 1):
            raise ValueError(f"{path}: palette replacement flags or scaling are unsupported")
        first_byte = descriptor["payload_offset"] + offset + (i - first) * 2
        payload.extend(body[first_byte:first_byte + 2])
    return payload


def texture_binding(texture, resources, entries, pngs, filter_mode):
    if "load" not in texture:
        raise ValueError("unresolved texture load")
    load = texture["load"]
    path = load["image"]["path"]
    tile, uv = texture["descriptor"], texture["uv"]
    _, version, endian, body = resources.typed_body(path, ("Texture",))
    source = o2r.parse_texture_descriptor(body, endian, version, path)
    width, height = source["width"], source["height"]
    if load["kind"] != "block" or load["source_offset"] or [width, height] != uv["tile_extent"]:
        raise ValueError(f"{path}: only full-resource LOADBLOCK textures are supported")
    if (source["flags"] or source["horizontal_byte_scale"] != 1 or source["vertical_pixel_scale"] != 1
            or not source["expected_packed_size_matches"] or load["total_bytes"] != source["payload_size"]):
        raise ValueError(f"{path}: texture flags, dimensions, scale or LOADBLOCK length do not match the backing image")
    expected_format = TEXTURE_FORMATS.get((tile["format"], tile["size"]))
    if expected_format is None or expected_format != source["format_code"]:
        raise ValueError(f"{path}: material tile format disagrees with its texture resource")
    payload = body[source["payload_offset"]:]
    binding = {"name": "scenes/placeholder.rgba16", "width": width, "height": height,
               "filter": "three_point" if filter_mode == 2 else "point", "address": [],
               "scale": [], "origin": []}
    wsc.texture_descriptor(binding)
    if "palette" in texture:
        palette = texture["palette"]
        pixels = wsc.decode_indexed_rgba({**binding, "index_bits": source["bits_per_pixel"],
                                         "indices": payload.hex(), "palette_format": palette["interpretation"],
                                         "palette": palette_bytes(resources, entries, palette).hex()})
    else:
        pixels = o2r.decode_texture_rgba(source, payload)
    for axis, dimension in zip(("s", "t"), (width, height)):
        params = uv[axis]
        mask = params["mask_period_texels"]
        if ((params["clamp"] and params["mirror"]) or mask not in (None, dimension)
                or params["mirror"] and mask != dimension):
            raise ValueError(f"{path}: unsupported {axis}-axis mask period or combined address mode")
        binding["address"].append("clamp" if params["clamp"] else "mirror" if params["mirror"] else "repeat")
        scale = Fraction(params["shift_multiplier"]) * 256
        if scale.denominator != 1 or not -32768 <= scale <= 32767:
            raise ValueError(f"{path}: {axis}-axis shift is outside WSC3 Q8.8 precision")
        binding["scale"].append(int(scale))
        binding["origin"].append(-8 * params["origin_10_2"])
    key = sha256(struct.pack("<HH", width, height) + pixels).hexdigest()
    name = binding["name"] = f"scenes/o2r-{key}.rgba16"
    pngs.setdefault(name, (width, height, pixels))
    return binding


def lower_material(draw, resources, entries, pngs):
    state, combiner = draw["render_state"], draw["combiner"]
    mode, known = draw["geometry"]["mode"], draw["geometry"]["known_mask"]
    if known & 0x0F0601 != 0x0F0601 or mode & 0x0F0200:
        raise ValueError("unknown or unsupported fog, lighting, texgen or front-face culling")
    blender, effective = state["reference_blender"], combiner["effective"]
    if blender is None or effective is None or state["depth_test"] is None or state["depth_write"] is None:
        raise ValueError("incomplete blender, combiner or depth state")
    translucent = draw["pass"] == "translucent"
    low, high = state["other_mode_low"], state["other_mode_high"]
    if (blender["fog"] or blender["blend_color"] or blender["invisible"] or blender["coverage_alpha"]
            or low & 4 or state["z_mode"] != 2 * translucent
            or state["alpha_compare"] != 0 or blender["alpha"] != translucent):
        raise ValueError(f"unsupported alpha, coverage or depth mode 0x{low:08x}")
    # Only proven straight-alpha equations for both cycles. Opaque accepts no
    # blend or the standard pixel/coverage blender with force-blend clear.
    valid_blenders = (0x0050,) if translucent else (0, 0x0055, 0x0044)
    if state["blender_raw"] not in valid_blenders or bool(low & 0x4000) != translucent:
        raise ValueError(f"unsupported blend equation 0x{low:08x}")
    inputs, colors = effective["texture_inputs"], combiner["colors"]
    rgb = combiner_channel(effective["rgb"], "rgb", inputs, colors)
    alpha = combiner_channel(effective["alpha"], "a", inputs, colors, opaque_alpha=not translucent)
    if translucent and rgb[3] != alpha[3]:
        raise ValueError("color and alpha use different texture equations")
    filter_mode = high >> 12 & 3
    bindings = []
    if inputs:
        if filter_mode not in (0, 2) or high & 0x70000 or not high & 0x80000:
            raise ValueError(f"unsupported texture filter, LOD, or perspective state 0x{high:08x}")
        bindings = [texture_binding(t, resources, entries, pngs, filter_mode) for t in draw["textures"]]
    if len(bindings) == 2 and rgb[3]:
        bindings.reverse()
    env_alpha = colors["environment"]["rgba"][3] if len(bindings) == 2 else 255
    material = {"textures": bindings, "depth_test": state["depth_test"], "depth_write": state["depth_write"],
                "cull_back": bool(mode & 0x400),
                "primitive": (rgb[1] if isinstance(rgb[1], list) else [rgb[1]] * 3) + [alpha[1]],
                "environment": (rgb[2] if isinstance(rgb[2], list) else [rgb[2]] * 3) + [env_alpha]}
    return material, rgb[0], alpha[0]


class Conversion:
    def __init__(self, resources, root, scale, offset, layout, expanded):
        self.resources, self.root = resources, root
        self.scale, self.offset, self.layout = scale, offset, layout
        self.boundary = 32768 if expanded else 8192
        self.pngs, self.batches, self.failures = {}, defaultdict(list), []

    def point(self, source):
        point = [round(n * self.scale) + offset for n, offset in zip(source, self.offset)]
        wsc.vector(point, -32768, 32767, 3, "transformed position")
        if not all(-self.boundary < point[i] < self.boundary for i in (0, 2)):
            raise ValueError(f"transformed position outside collision grid: {point}; use --scale, --offset or --expanded-grid")
        return point

    def vertex(self, path, index):
        # The tracer validated the typed resource, byte count, and source
        # window before storing this reference. Read only the selected vertex.
        kind, _, endian, body = self.resources.typed_body(path, ("Array", "Vertex"))
        header = 4 if kind == "Vertex" else 8
        values = struct.unpack_from(endian + "3hH2h4B", body, header + index * 16)
        return values[:3], values[4:6], values[6:]

    def draw(self, draw, refs, palettes):
        try:
            if draw["missing"] or draw["approximations"]:
                raise ValueError("; ".join(draw["missing"] + draw["approximations"]))
            material, shade_rgb, shade_alpha = lower_material(draw, self.resources, palettes, self.pngs)
            vertices = []
            for path, index, scale, geometry in refs:
                if geometry:
                    raise ValueError("lighting or texgen applied at vertex load; pre-bake vertex colors and UVs in the source")
                position, source_uv, cn = self.vertex(path, index)
                uv = [0, 0] if not material["textures"] else [
                    coordinate * factor >> 16 for coordinate, factor in zip(source_uv, scale)]
                color = (list(cn[:3]) if shade_rgb else WHITE[:3]) + [cn[3] if shade_alpha else 255]
                vertices.append({"position": self.point(position), "uv": uv, "color": color})
            for i in range(0, len(vertices), 3):
                if not any(cross(*(v["position"] for v in vertices[i:i + 3]))):
                    raise ValueError("render triangle becomes degenerate after coordinate conversion")
            batches = self.batches[draw["room"]]
            if batches and batches[-1]["pass"] == draw["pass"] and batches[-1]["material"] == material:
                batches[-1]["render_vertices"].extend(vertices)
            else:
                batches.append({"pass": draw["pass"], "material": material, "render_vertices": vertices})
        except (ValueError, o2r.ImportFailure, trace.TraceError) as error:
            self.failures.append({"room": draw["room"], "pass": draw["pass"], "display_list": draw["display_list"],
                                  "offset": draw["offset"], "command": draw["command"], "reason": str(error)})

    def collision(self, path):
        _, _, endian, body = self.resources.typed_body(path, ("CollisionHeader",))
        collision = o2r.parse_collision(body, endian, path)
        if collision["water_boxes"]:
            raise ValueError(f"{path}: WSC3 cannot register source water boxes")
        if len(collision["vertices"]) > 16384 or len(collision["polygons"]) > 16 * 8192:
            raise ValueError(f"{path}: collision exceeds WSC region limits")
        vertices = [self.point(vertex) for vertex in collision["vertices"]]
        triangles = []
        for index, poly in enumerate(collision["polygons"]):
            indices = poly["vertex_indices"]
            if any(poly["vertex_flags"]):
                raise ValueError(f"{path}: polygon {index} has unsupported collision participation flags")
            normal = cross(*(vertices[i] for i in indices))
            if not any(normal) or sum(a * b for a, b in zip(normal, poly["normal"])) <= 0:
                raise ValueError(f"{path}: polygon {index} is degenerate or disagrees with its source winding")
            surface = poly["surface_type"]
            if surface >= len(collision["surface_types"]):
                raise ValueError(f"{path}: polygon {index} has an invalid surface index")
            mapping = self.layout.get("surface_map", {})
            if str(surface) in mapping:
                terrain = wsc.integer(mapping[str(surface)], 0, 32767, "mapped SM64 surface")
            elif collision["surface_types"][surface] == [0, 0]:
                terrain = 0
            else:
                raise ValueError(f"{path}: polygon {index} needs a surface_map entry for source surface {surface}")
            triangles.append({"indices": indices, "type": terrain})
        return vertices, triangles

    def convert(self, segments):
        source = self.resources.read(self.root, ("Scene",))
        room_paths = [item["path"] for item in command_of(source, 4, self.root).get("rooms", [])] or [self.root]
        if len(room_paths) > 16 or len(set(room_paths)) != len(room_paths):
            raise ValueError("scene requires 1–16 distinct room resources")
        preludes = self.layout.get("pass_preludes", {})
        if not isinstance(preludes, dict) or set(preludes) - {"opaque", "translucent"}:
            raise ValueError("pass_preludes accepts opaque/translucent display-list paths")
        preludes = {key: o2r.safe_archive_path(path) for key, path in preludes.items()}
        report = trace.DrawTracer(self.resources, self.root, segments, draw_sink=self.draw,
                                  pass_preludes=preludes).run()
        if report["issues"] or self.failures:
            return None, {"status": "rejected", "trace_issues": report["issues"], "draw_issues": self.failures,
                          "candidate_draws": len(report["draws"])}
        if set(report["rooms"]) != set(room_paths):
            raise ValueError("every room must have a normal mesh and at least one supported triangle")
        rooms = [{"id": i + 1, "batches": self.batches[path], "neighbors": []} for i, path in enumerate(room_paths)]
        connect_rooms(rooms, command_of(source, 14, self.root).get("transition_actors", []),
                      self.layout.get("links", []))
        root_collision = command_of(source, 3, self.root).get("collision_header")
        room_collision = [command_of(self.resources.read(path, ("Scene",)), 3, path).get("collision_header")
                          for path in room_paths]
        if len(rooms) == 1 and root_collision == room_collision[0]:
            root_collision = None
        if root_collision and any(room_collision):
            raise ValueError("scene-wide and room-local collision coexist; select one ownership model")
        if root_collision:
            vertices, triangles = self.collision(root_collision)
            owners = self.layout.get("collision_rooms", [0] * len(triangles) if len(rooms) == 1 else [])
            if len(owners) != len(triangles):
                raise ValueError("scene-wide multiroom collision requires one collision_rooms entry per source polygon")
            for i, room in enumerate(rooms):
                assign_collision(room, vertices, [triangle for triangle, owner in zip(triangles, owners)
                                                if wsc.integer(owner, 0, len(rooms) - 1, "collision room") == i])
        else:
            if not all(room_collision) or len(set(room_collision)) != len(rooms):
                raise ValueError("every room requires a distinct collision header or a scene-wide header")
            for room, path in zip(rooms, room_collision):
                assign_collision(room, *self.collision(path))
        apply_bounds(rooms, self.layout.get("bounds"))
        entrances = self.entrances(source, rooms)
        metadata = sorted({f"{path}: {command['name']} metadata has no WSC gameplay mapping"
                           for path in dict.fromkeys([self.root, *room_paths])
                           for command in self.resources.read(path, ("Scene",))
                           if command["id"] not in (0, 3, 4, 6, 8, 10, 14, 20)})
        if commands_of(source, 14):
            metadata.append("Transition actors provide room adjacency only; door actors and camera effects are not instantiated")
        scene = {"version": 3, "id": SCENE_ID, "rooms": rooms, "entrances": entrances}
        return scene, {"status": "converted", "source_scene": self.root, "source_room_paths": room_paths,
                       "candidate_draws": len(report["draws"]), "texture_count": len(self.pngs),
                       "rooms": [{"id": room["id"], "neighbors": room["neighbors"], "batches": len(room["batches"]),
                                  "render_triangles": sum(len(batch["render_vertices"]) // 3 for batch in room["batches"]),
                                  "collision_triangles": len(room["collision_triangles"])} for room in rooms],
                       "entrances": entrances, "source_default_entrance": self.layout.get("entrance", 0),
                       "pass_preludes": preludes, "unmapped_metadata": metadata}

    def entrances(self, source, rooms):
        actors = command_of(source, 0, self.root).get("entries", [])
        rows = command_of(source, 6, self.root).get("entrances", [])
        if not rows:
            raise ValueError("source scene has no entrance table")
        entrances = []
        for index, row in enumerate(rows):
            room_id = wsc.integer(row["room"], 0, len(rooms) - 1, "entrance room")
            actor = actors[wsc.integer(row["spawn"], 0, len(actors) - 1, "entrance spawn")]
            point = self.point(actor["position"])
            room = rooms[room_id]
            position = safe_spawn(room, point)
            if position is None:
                raise ValueError(f"entrance {index} has no safe local floor near {point}; check the source spawn and collision")
            entrances.append({"id": index, "room": room["id"], "position": position, "yaw": actor["rotation"][1]})
        selected = wsc.integer(self.layout.get("entrance", 0), 0, len(entrances) - 1, "default entrance")
        entrances[0]["id"], entrances[selected]["id"] = selected, 0
        return entrances


def cross(a, b, c):
    u, v = [x - y for x, y in zip(b, a)], [x - y for x, y in zip(c, a)]
    return [u[(i + 1) % 3] * v[(i + 2) % 3] - u[(i + 2) % 3] * v[(i + 1) % 3] for i in range(3)]


def connect_rooms(rooms, transitions, links):
    for a, b in [[entry["front_room"], entry["back_room"]] for entry in transitions] + links:
        if a == 255 or b == 255 or a == b:
            continue
        a = wsc.integer(a, 0, len(rooms) - 1, "front room")
        b = wsc.integer(b, 0, len(rooms) - 1, "back room")
        for room, neighbor in ((a, b), (b, a)):
            if neighbor + 1 not in rooms[room]["neighbors"]:
                rooms[room]["neighbors"].append(neighbor + 1)
    for room in rooms:
        room["neighbors"].sort()


def assign_collision(room, vertices, triangles):
    if not triangles:
        raise ValueError(f"room {room['id']} has no collision")
    used = sorted({i for triangle in triangles for i in triangle["indices"]})
    indices = {source: index for index, source in enumerate(used)}
    room["collision_vertices"] = [vertices[i] for i in used]
    room["collision_triangles"] = [{**tri, "indices": [indices[i] for i in tri["indices"]]} for tri in triangles]


def apply_bounds(rooms, authored):
    if authored is not None and len(authored) != len(rooms):
        raise ValueError("provide one bounds record per room")
    for index, room in enumerate(rooms):
        points = room["collision_vertices"] + [v["position"] for batch in room["batches"] for v in batch["render_vertices"]]
        room["min"] = [min(axis) for axis in zip(*points)]
        room["max"] = [max(axis) for axis in zip(*points)]
        room["min"][1] = max(-32768, room["min"][1] - 256)
        room["max"][1] = min(32767, room["max"][1] + 512)
        if authored is not None:
            room["min"], room["max"] = authored[index]["min"], authored[index]["max"]


def safe_spawn(room, source):
    # Use the actual converted faces and preserve source X/Z. Check Mario's
    # horizontal footprint and ceiling clearance before choosing a spawn.
    x, y, z = source
    heights = []
    tiles = []
    for tri in room["collision_triangles"]:
        a, b, c = vertices = [room["collision_vertices"][i] for i in tri["indices"]]
        nx, ny, nz = cross(a, b, c)
        if not ny:
            continue
        tiles.append((vertices, nx, ny, nz, tri["type"]))
    for dx, dz in ((0, 0), (-50, 0), (50, 0), (0, -50), (0, 50),
                   (-35, -35), (-35, 35), (35, -35), (35, 35)):
        px, pz = x + dx, z + dz
        floors, ceilings = [], []
        for vertices, nx, ny, nz, terrain in tiles:
            a, _, _ = vertices
            oriented_edges = [(v[2] - u[2]) * (px - u[0]) - (v[0] - u[0]) * (pz - u[2])
                              for u, v in zip(vertices, vertices[1:] + vertices[:1])]
            if any(edge * ny < 0 for edge in oriented_edges):
                continue
            height = Fraction(a[1]) - Fraction(nx * (px - a[0]) + nz * (pz - a[2]), ny)
            if ny > 0 and height <= y + 40:
                floors.append((height, terrain == 0 and 2 * ny * ny >= nx * nx + ny * ny + nz * nz))
            elif ny < 0:
                ceilings.append(height)
        if not floors:
            return None
        height, walkable = max(floors)
        if not walkable or not -256 <= height - y <= 40 or any(height <= top < height + 180 for top in ceilings):
            return None
        heights.append(height)
    if max(heights) - min(heights) > 50:
        return None
    height = -(-heights[0] // 1)
    result = [x, height + 16, z]
    return result if wsc.inside(result, room["min"], room["max"]) else None


def convert_archive(archive_path, source, layout, scale, offset, expanded, segments):
    if archive_path.stat().st_size > trace.MAX_ARCHIVE_FILE_BYTES:
        raise ValueError("archive file exceeds the O2R size limit")
    if scale <= 0 or scale > 1024:
        raise ValueError("--scale must be positive and no greater than 1024")
    allowed = {"bounds", "collision_rooms", "links", "surface_map", "entrance", "pass_preludes"}
    if not isinstance(layout, dict) or set(layout) - allowed:
        raise ValueError("invalid layout fields")
    with ZipFile(archive_path) as archive:
        conversion = Conversion(trace.ArchiveResources(archive), o2r.safe_archive_path(source),
                                scale, offset, layout, expanded)
        scene, report = conversion.convert(segments)
        return (wsc.encode_scene(scene) if scene else None), conversion.pngs, report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("scene", help="exact scene resource path inside the archive")
    parser.add_argument("output", type=Path, help="new directory to stage for copying into app Documents")
    parser.add_argument("--layout", type=Path, help="optional scene-independent JSON ownership and room metadata")
    parser.add_argument("--scale", type=Fraction, default=Fraction(1), help="units multiplier, e.g. 1/2")
    parser.add_argument("--offset", type=int, nargs=3, default=(0, 0, 0), metavar=("X", "Y", "Z"))
    parser.add_argument("--expanded-grid", action="store_true", help="target a build with the expanded collision grid")
    parser.add_argument("--segment", action="append", default=[], metavar="HEX=RESOURCE")
    args = parser.parse_args(argv)
    try:
        if args.output.exists() or args.output.is_symlink():
            raise ValueError("output must be a new directory")
        layout = {}
        if args.layout:
            if args.layout.stat().st_size > MAX_LAYOUT_BYTES:
                raise ValueError("layout exceeds byte limit")
            layout = json.loads(args.layout.read_text(encoding="utf-8"))
        segments = {}
        for value in args.segment:
            number, path = value.split("=", 1)
            number = int(number, 16)
            if number in segments or not 0 <= number < 16:
                raise ValueError(f"duplicate or out-of-range segment {number:#x}")
            segments[number] = trace.Reference(o2r.safe_archive_path(path))
        package, pngs, report = convert_archive(args.archive, args.scene, layout, args.scale,
                                               args.offset, args.expanded_grid, segments)
        encoded_report = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if package is None:
            sys.stderr.write(encoded_report)
            return 2
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with TemporaryDirectory(dir=args.output.parent, prefix=".o2r-wsc3-") as stage:
            output = Path(stage) / "scene"
            (output / "scenes").mkdir(parents=True)
            for name, (width, height, pixels) in sorted(pngs.items()):
                o2r.write_rgba_png(output / "gfx" / (name + ".png"), width, height, pixels)
            (output / "scenes" / f"{SCENE_ID}.wsc").write_bytes(package)
            (output / "conversion-report.json").write_text(encoded_report, encoding="utf-8")
            if args.output.exists():
                raise ValueError("output already exists")
            output.rename(args.output)
    except (OSError, BadZipFile, KeyError, TypeError, ValueError, ZeroDivisionError, struct.error, o2r.ImportFailure, trace.TraceError) as error:
        print(f"o2r-to-wsc3: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
