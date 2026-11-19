#!/usr/bin/env python3
"""Convert a supported, authorized O2R scene into a local Documents import tree."""

import argparse
from collections import Counter, defaultdict
from fractions import Fraction
from hashlib import sha256
import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
from zipfile import BadZipFile, ZipFile

if __package__:
    from . import fast3d_pass_state, hyrule_importer as o2r, o2r_draw_trace as trace, world_scene_package as wsc
else:
    import fast3d_pass_state
    import hyrule_importer as o2r
    import o2r_draw_trace as trace
    import world_scene_package as wsc

SCENE_ID = 4100
MAX_LAYOUT_BYTES = 1024 * 1024
TEXTURE_FORMATS = {(0, 2): 2, (0, 3): 1, (2, 0): 3, (2, 1): 4,
                   (3, 0): 7, (3, 1): 8, (3, 2): 9, (4, 0): 5, (4, 1): 6}
WHITE = [255] * 4
HYRULE_XLU_APPROXIMATION = "Hyrule 0xc8104b50 translucent coverage mode lowered to native straight-alpha blending"
VALIDATED_APPROXIMATIONS = {"Fast3D CI decode uses RGBA5551 despite disabled RDP TLUT",
                            HYRULE_XLU_APPROXIMATION}


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


def contains_expression(expression, value):
    if expression == value:
        return True
    return (isinstance(expression, (tuple, list))
            and any(contains_expression(item, value) for item in expression[1:]))


def fold_static_primitive_lod_alpha(expression, primitive_lod_fraction):
    """Fold only the OoT combine form proven constant by its draw-list state."""
    if (isinstance(expression, (tuple, list)) and len(expression) == 5
            and expression[0] == "combine" and expression[1] == "tex0.a"
            and expression[2] == 0 and expression[3] == "primitive_lod_fraction"
            and expression[4] == ("multiply", "primitive.a", "tex0.a")):
        if primitive_lod_fraction == 0:
            return expression[4]
        raise ValueError("Fast3D alpha LOD interpolation is nonzero and cannot be lowered statically")
    if contains_expression(expression, "primitive_lod_fraction"):
        raise ValueError("unsupported Fast3D primitive-LOD alpha equation")
    return expression


def combiner_channel(expression, channel, inputs, colors, opaque_alpha=False,
                     allow_texture_independent=False):
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
    if inputs and reverse is None and not allow_texture_independent:
        raise ValueError(f"{channel} channel ignores a texture sampled by the other channel")
    return shade, primitive, environment, reverse


def alpha_bytes_are_opaque(pixels):
    return bool(pixels) and len(pixels) % 4 == 0 and all(alpha == 255 for alpha in pixels[3::4])


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
    if (source["flags"] or source["horizontal_byte_scale"] != 1 or source["vertical_pixel_scale"] != 1
            or not source["expected_packed_size_matches"]):
        raise ValueError(f"{path}: texture flags, dimensions or scale do not match a packed image")
    expected_format = TEXTURE_FORMATS.get((tile["format"], tile["size"]))
    if expected_format is None or expected_format != source["format_code"]:
        raise ValueError(f"{path}: material tile format disagrees with its texture resource")
    payload = body[source["payload_offset"]:]
    if load["kind"] == "block":
        if load["source_offset"] or [width, height] != uv["tile_extent"] or load["total_bytes"] != len(payload):
            raise ValueError(f"{path}: LOADBLOCK must cover the whole backing texture")
    else:
        image = load["image"]
        bits_per_pixel = source["bits_per_pixel"]
        if (bits_per_pixel not in (4, 8, 16) or image["size_bits"] != bits_per_pixel
                or image["format"] != tile["format"] or image["offset"] or image["width"] != width
                or any(value % 4 for value in load["bounds_10_2"])):
            raise ValueError(f"{path}: LOADTILE requires packed 4-, 8-, or 16-bit source texels and matching source stride")
        uls, ult, _, _ = load["bounds_10_2"]
        left, top = uls // 4, ult // 4
        crop_width, crop_height = load["width"], load["height"]
        row_bytes = (crop_width * bits_per_pixel + 7) // 8
        tmem_stride = ((row_bytes + 7) // 8) * 8
        if (left + crop_width > width or top + crop_height > height
                or uv["tile_extent"] != [crop_width, crop_height]
                or load["destination_stride_bytes"] != tmem_stride or tile["line_words"] * 8 != tmem_stride):
            raise ValueError(f"{path}: LOADTILE rectangle, TMEM stride, or render tile extent disagrees")
        stride = (width * bits_per_pixel + 7) // 8
        if bits_per_pixel == 4:
            payload = crop_ci4(payload, width, height, left, top, crop_width, crop_height)
        else:
            texel_bytes = bits_per_pixel // 8
            start = (top * width + left) * texel_bytes
            payload = b"".join(payload[start + y * stride:start + y * stride + row_bytes]
                               for y in range(crop_height))
        width, height = crop_width, crop_height
        source.update(width=width, height=height, image_data_size=len(payload))
    binding = {"name": "scenes/placeholder.rgba16", "width": width, "height": height,
               "filter": "three_point" if filter_mode == 2 else "point", "address": [],
               "scale": [], "origin": []}
    # The importer produces WSC5 when the traced material set needs its decal
    # extension. Validate the source against the broadest target first; the
    # final version-specific package encoder still rejects oversized textures
    # for WSC1-WSC4.
    wsc.texture_descriptor(binding, 5)
    if "palette" in texture:
        palette = texture["palette"]
        palette_format = palette.get("interpretation") or palette.get("reference_interpretation")
        if palette_format not in ("rgba5551", "ia88"):
            raise ValueError(f"{path}: indexed texture has no explicit supported palette interpretation")
        pixels = wsc.decode_indexed_rgba({**binding, "index_bits": source["bits_per_pixel"],
                                         "indices": payload.hex(), "palette_format": palette_format,
                                         "palette": palette_bytes(resources, entries, palette).hex()}, 5)
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


def crop_ci4(payload, source_width, source_height, left, top, width, height):
    """Crop high-nibble-first CI4 rows, including an odd source X origin."""
    if len(payload) != (source_width * source_height + 1) // 2:
        raise ValueError("CI4 payload size does not match its source dimensions")
    if min(left, top) < 0 or width <= 0 or height <= 0 or left + width > source_width or top + height > source_height:
        raise ValueError("CI4 crop is outside its source texture")
    result = bytearray((width * height + 1) // 2)
    for y in range(height):
        for x in range(width):
            source_index = (top + y) * source_width + left + x
            packed = payload[source_index // 2]
            value = (packed >> 4) & 15 if source_index % 2 == 0 else packed & 15
            target_index = y * width + x
            if target_index % 2 == 0:
                result[target_index // 2] = value << 4
            else:
                result[target_index // 2] |= value
    return bytes(result)


def lower_material(draw, resources, entries, pngs, *, vertex_alpha_opaque=False):
    state, combiner = draw["render_state"], draw["combiner"]
    mode, known = draw["geometry"]["mode"], draw["geometry"]["known_mask"]
    if known & 0x0F0601 != 0x0F0601 or mode & 0x0C0200:
        raise ValueError("unknown or unsupported texgen or front-face culling state")
    blender, effective = state["reference_blender"], combiner["effective"]
    if blender is None or effective is None or state["depth_test"] is None or state["depth_write"] is None:
        raise ValueError("incomplete blender, combiner or depth state")
    fog_factors = draw["fog"].get("vertex_factors", [])
    fog_color = draw["fog"].get("color")
    fog_available = (draw["fog"].get("enabled") and len(fog_factors) == 1
                     and isinstance(fog_factors[0], dict) and fog_color
                     and len(fog_color.get("rgba", [])) == 4)
    fog_effective = bool(fog_available and (fog_factors[0]["multiplier"] or
                                            fog_factors[0]["offset_coefficient"]))
    if blender["fog"] and not fog_available:
        raise ValueError("RDP fog blender is active without one complete homogeneous Fast3D fog state")
    translucent = draw["pass"] == "translucent"
    blended = translucent
    low, high = state["other_mode_low"], state["other_mode_high"]
    cutout = False
    decal = False
    if low >> 30 == 3:
        hyrule_modes = {
            0xC8112078: ("opaque", False),
            0xC8113078: ("opaque", True),
            0xC81049D8: ("translucent", False),
            0xC8104B50: ("translucent", False),
            0xC8104DD8: ("opaque", False),
            0xC8104F50: ("translucent", False),
        }
        render_mode = hyrule_modes.get(low)
        if render_mode is None or render_mode[0] != draw["pass"]:
            raise ValueError(f"Fast3D render mode 0x{low:08x} is outside the validated Hyrule opaque/cutout/XLU subset")
        cutout = render_mode[1]
        decal = low in (0xC8104DD8, 0xC8104F50)
        if decal:
            blended = True
        if decal and (not state["depth_test"] or state["depth_write"]
                      or state["z_mode"] != 3 or state["alpha_compare"] != 0):
            raise ValueError("Hyrule decal render mode has inconsistent depth or alpha state")
        if decal and not state["force_blend"]:
            raise ValueError("Hyrule decal render mode does not enable force blend")
        # A TEX_EDGE render mode only discards when the combiner alpha is
        # below the edge threshold. If the source alpha equation is the
        # literal constant 1, the test can never discard a pixel, so the
        # equivalent WSC material is ordinary opaque. Do not infer this from
        # missing alpha inputs alone: explicit constants other than 1 must be
        # rejected rather than changed into visible geometry.
        has_alpha_source = any(value.endswith(".a") for value in effective["inputs"])
        if cutout and not has_alpha_source:
            if effective["alpha"] == 1:
                cutout = False
            else:
                raise ValueError("Hyrule texture-edge mode has no alpha source and is not constant opaque")
        if low == 0xC8104B50:
            draw["approximations"].append(HYRULE_XLU_APPROXIMATION)
    else:
        if (blender["blend_color"] or blender["invisible"] or blender["coverage_alpha"]
                or low & 4 or state["z_mode"] != 2 * translucent or state["alpha_compare"] != 0
                or blender["alpha"] != translucent):
            raise ValueError(f"unsupported alpha, coverage or depth mode 0x{low:08x}")
    if (blender["blend_color"] or blender["invisible"] or blender["coverage_alpha"]
            or low & 4 or state["z_mode"] != 2 * translucent
            or state["alpha_compare"] != 0 or (blender["alpha"] != translucent and not cutout)) and low >> 30 != 3:
        raise ValueError(f"unsupported alpha, coverage or depth mode 0x{low:08x}")
    # Only proven straight-alpha equations for both cycles. Opaque accepts no
    # blend or the standard pixel/coverage blender with force-blend clear.
    valid_blenders = (0x0050,) if translucent else (0, 0x0055, 0x0044)
    if low >> 30 != 3 and (state["blender_raw"] not in valid_blenders or bool(low & 0x4000) != translucent):
        raise ValueError(f"unsupported blend equation 0x{low:08x}")
    inputs, colors = effective["texture_inputs"], combiner["colors"]
    alpha_expression = effective["alpha"]
    if low == 0xC8104F50:
        alpha_expression = fold_static_primitive_lod_alpha(
            alpha_expression, colors["primitive"].get("lod_fraction"))
    independent_primitive_alpha = (blended and len(inputs) == 2
                                   and alpha_expression == "primitive.a")
    rgb = combiner_channel(effective["rgb"], "rgb", inputs, colors)
    alpha = combiner_channel(alpha_expression, "a", inputs, colors,
                             opaque_alpha=not blended and not cutout,
                             allow_texture_independent=independent_primitive_alpha)
    if blended and alpha[3] is not None and rgb[3] != alpha[3]:
        raise ValueError("color and alpha use different texture equations")
    filter_mode = high >> 12 & 3
    bindings = []
    if inputs:
        if filter_mode not in (0, 2) or high & 0x70000 or not high & 0x80000:
            raise ValueError(f"unsupported texture filter, LOD, or perspective state 0x{high:08x}")
        bindings = [texture_binding(t, resources, entries, pngs, filter_mode) for t in draw["textures"]]
    if len(bindings) == 2 and rgb[3]:
        bindings.reverse()
    if blended and not alpha[0] and not vertex_alpha_opaque:
        raise ValueError("source alpha omits shade alpha but referenced vertex alpha is not proven opaque")
    if blended and bindings and alpha[3] is None:
        if any(not alpha_bytes_are_opaque(pngs[binding["name"]][2]) for binding in bindings):
            raise ValueError("source alpha omits texture alpha but decoded texture alpha is not proven opaque")
    if independent_primitive_alpha:
        # The current Metal WSC material shader multiplies the sampled alpha,
        # vertex alpha, and primitive alpha. OoT's source equation here uses
        # primitive alpha alone, so it is equivalent only when the additional
        # alpha factors are identically one for every referenced sample/vertex.
        if not vertex_alpha_opaque:
            raise ValueError("primitive-only dual-texture alpha needs opaque source vertex alpha")
        if len(bindings) != 2 or any(not alpha_bytes_are_opaque(pngs[binding["name"]][2])
                                     for binding in bindings):
            raise ValueError("primitive-only dual-texture alpha needs fully opaque decoded texture alpha")
    env_alpha = colors["environment"]["rgba"][3] if len(bindings) == 2 else 255
    material = {"textures": bindings, "depth_test": state["depth_test"], "depth_write": state["depth_write"],
                "cutout": cutout, "cull_back": bool(mode & 0x400),
                "primitive": (rgb[1] if isinstance(rgb[1], list) else [rgb[1]] * 3) + [alpha[1]],
                "environment": (rgb[2] if isinstance(rgb[2], list) else [rgb[2]] * 3) + [env_alpha]}
    if decal:
        material["decal"] = True
        material["blend"] = True
    if fog_effective:
        material["fog"] = {"multiplier": fog_factors[0]["multiplier"],
                            "offset": fog_factors[0]["offset_coefficient"],
                            "color": fog_color["rgba"]}
    return material, rgb[0], alpha[0]


class Conversion:
    def __init__(self, resources, root, scale, offset, layout, expanded):
        self.resources, self.root = resources, root
        self.scale, self.offset, self.layout = scale, offset, layout
        self.boundary = 32768 if expanded else 8192
        self.pngs, self.batches, self.failures = {}, defaultdict(list), []
        self.converted_sources = set()
        self.vertex_alpha_cache = {}
        self.accepted_approximation_counts = Counter()
        self.report = {"schema_version": 1, "status": "rejected", "source_scene": root,
                       "source_room_paths": [], "candidate_draws": 0, "rooms": [], "entrances": [],
                       "trace_issues": [], "draw_issues": self.failures, "scene_issues": [],
                       "rendering_approximations": [], "collision_approximations": [],
                       "entrance_approximations": []}

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
        normal = [value - 256 if value >= 128 else value for value in values[6:9]]
        return values[:3], values[4:6], values[6:], normal

    def draw(self, draw, refs, palettes):
        try:
            rejected_approximations = sorted(set(draw["approximations"]) - VALIDATED_APPROXIMATIONS)
            if draw["missing"] or rejected_approximations:
                raise ValueError("; ".join(draw["missing"] + rejected_approximations))
            effective = draw["combiner"]["effective"]
            needs_opaque_vertex_alpha = (draw["pass"] == "translucent"
                                         or draw["render_state"].get("force_blend") is True)
            vertex_alpha_opaque = (self.source_vertex_alpha_opaque(refs)
                                   if needs_opaque_vertex_alpha
                                   and not contains_expression(effective["alpha"], "shade.a")
                                   else False)
            material, shade_rgb, shade_alpha = lower_material(
                draw, self.resources, palettes, self.pngs,
                vertex_alpha_opaque=vertex_alpha_opaque)
            rejected_approximations = sorted(set(draw["approximations"]) - VALIDATED_APPROXIMATIONS)
            if rejected_approximations:
                raise ValueError("; ".join(rejected_approximations))
            for approximation in set(draw["approximations"]) & VALIDATED_APPROXIMATIONS:
                self.accepted_approximation_counts[approximation] += 1
            vertices = []
            for path, index, scale, geometry, matrix, fog_factor, num_lights, light_state in refs:
                if geometry & (trace.G_TEXTURE_GEN | trace.G_TEXTURE_GEN_LINEAR):
                    raise ValueError("generated texture coordinates need transformed normals and texgen state, which are not lowered")
                unsupported_geometry = geometry & ~(trace.G_FOG | trace.G_LIGHTING)
                if unsupported_geometry:
                    raise ValueError(f"unsupported vertex geometry mode bits 0x{unsupported_geometry:05x}")
                position, source_uv, cn, normal = self.vertex(path, index)
                position = transform_position(position, matrix)
                uv = [0, 0] if not material["textures"] else [
                    coordinate * factor >> 16 for coordinate, factor in zip(source_uv, scale)]
                if geometry & trace.G_LIGHTING and shade_rgb:
                    lights = json.loads(light_state)
                    count = json.loads(num_lights)["count"]
                    color_rgb = rsp_vertex_lighting(normal, matrix, lights, count)
                else:
                    color_rgb = list(cn[:3]) if shade_rgb else WHITE[:3]
                color = color_rgb + [cn[3] if shade_alpha else 255]
                vertices.append({"position": self.point(position), "uv": uv, "color": color})
            for i in range(0, len(vertices), 3):
                if not any(cross(*(v["position"] for v in vertices[i:i + 3]))):
                    raise ValueError("render triangle becomes degenerate after coordinate conversion")
            batches = self.batches[draw["room"]]
            if batches and batches[-1]["pass"] == draw["pass"] and batches[-1]["material"] == material:
                batches[-1]["render_vertices"].extend(vertices)
            else:
                batches.append({"pass": draw["pass"], "material": material, "render_vertices": vertices})
            self.converted_sources.update({draw["display_list"], *(ref[0] for ref in refs)})
            for texture in draw["textures"]:
                self.converted_sources.add(texture["load"]["image"]["path"])
                self.converted_sources.update(entry["path"] for entry in texture.get("palette", {}).get("sources", []))
        except (ValueError, o2r.ImportFailure, trace.TraceError) as error:
            self.failures.append({"room": draw["room"], "pass": draw["pass"], "display_list": draw["display_list"],
                                  "offset": draw["offset"], "command": draw["command"], "reason": str(error)})

    def source_vertex_alpha_opaque(self, refs):
        for reference in refs:
            path, index = reference[0], reference[1]
            if path not in self.vertex_alpha_cache:
                _, _, endian, body = self.resources.typed_body(path, ("Vertex", "Array"))
                vertices = o2r.parse_vertex_array(body, endian, path)["vertices"]
                self.vertex_alpha_cache[path] = [vertex["cn_bytes"][3] for vertex in vertices]
            alphas = self.vertex_alpha_cache[path]
            if not 0 <= index < len(alphas) or alphas[index] != 255:
                return False
        return True

    def collision(self, path):
        _, _, endian, body = self.resources.typed_body(path, ("CollisionHeader",))
        collision = o2r.parse_collision(body, endian, path)
        ignored_flag_pairs = collision_flag_policy(self.layout)
        if collision["water_boxes"]:
            if self.layout.get("ignore_water_boxes") is not True:
                raise ValueError(f"{path}: WSC runtime has no water-box gameplay representation; set ignore_water_boxes=true to explicitly omit swimming/water-level behavior")
            self.report["rendering_approximations"].append({"resource": path,
                "reason": "source water boxes explicitly omitted by layout flag; swimming and water-level gameplay are absent",
                "count": len(collision["water_boxes"])})
        if len(collision["vertices"]) > 16384 or len(collision["polygons"]) > 16 * 8192:
            raise ValueError(f"{path}: collision exceeds WSC region limits")
        vertices = [self.point(vertex) for vertex in collision["vertices"]]
        triangles = []
        mapped_surfaces = Counter()
        ignored_vertex_flags = Counter()
        for index, poly in enumerate(collision["polygons"]):
            indices = poly["vertex_indices"]
            flags = tuple(poly["vertex_flags"])
            if any(flags):
                if flags not in ignored_flag_pairs:
                    raise ValueError(f"{path}: polygon {index} has unsupported source collision corner flags {list(flags)}")
                ignored_vertex_flags[flags] += 1
            normal = cross(*(vertices[i] for i in indices))
            if not any(normal) or sum(a * b for a, b in zip(normal, poly["normal"])) <= 0:
                raise ValueError(f"{path}: polygon {index} is degenerate or disagrees with its source winding")
            surface = poly["surface_type"]
            if surface >= len(collision["surface_types"]):
                raise ValueError(f"{path}: polygon {index} has an invalid surface index")
            mapping = self.layout.get("surface_map", {})
            if str(surface) in mapping:
                terrain = wsc.integer(mapping[str(surface)], 0, 32767, "mapped SM64 surface")
                mapped_surfaces[surface] += 1
            elif collision["surface_types"][surface] == [0, 0]:
                terrain = 0
            else:
                raise ValueError(f"{path}: polygon {index} needs a surface_map entry for source surface {surface}")
            triangles.append({"indices": indices, "type": terrain})
        for source_surface, polygon_count in sorted(mapped_surfaces.items()):
            self.report["collision_approximations"].append({
                "resource": path, "source_surface_index": source_surface,
                "source_parameters": collision["surface_types"][source_surface],
                "sm64_surface": wsc.integer(self.layout["surface_map"][str(source_surface)],
                                             0, 32767, "mapped SM64 surface"),
                "polygon_count": polygon_count,
                "reason": "explicit layout mapping; OoT surface behavior and parameters are not translated",
            })
        for flags, polygon_count in sorted(ignored_vertex_flags.items()):
            self.report["collision_approximations"].append({
                "resource": path, "source_corner_flags": list(flags),
                "polygon_count": polygon_count,
                "reason": "source collision corner flags were explicitly omitted by layout; semantics are not translated",
            })
        self.converted_sources.add(path)
        return vertices, triangles

    def scene_profile(self):
        context = self.layout.get("hyrule_context")
        if context is None:
            return None
        if self.root != fast3d_pass_state.HYRULE_SCENE:
            raise ValueError("hyrule_context is restricted to the exact NTSC 1.0 Hyrule Field scene resource")
        if not isinstance(context, dict) or set(context) != {"frame", "lighting_setting", "daytime"}:
            raise ValueError("hyrule_context requires exactly frame, lighting_setting, and daytime")
        marker = self.resources.archive.read("version")[:len(fast3d_pass_state.NTSC_10)]
        if marker != fast3d_pass_state.NTSC_10:
            raise ValueError("hyrule_context requires the OoT NTSC 1.0 archive marker")
        return fast3d_pass_state.HyruleFieldInputs(context["frame"], context["lighting_setting"],
                                                    context["daytime"])

    def convert(self, segments):
        source = self.resources.read(self.root, ("Scene",))
        room_paths = [item["path"] for item in command_of(source, 4, self.root).get("rooms", [])] or [self.root]
        self.report["source_room_paths"] = room_paths
        if len(room_paths) > 16 or len(set(room_paths)) != len(room_paths):
            raise ValueError("scene requires 1–16 distinct room resources")
        preludes = self.layout.get("pass_preludes", {})
        if not isinstance(preludes, dict) or set(preludes) - {"opaque", "translucent"}:
            raise ValueError("pass_preludes accepts opaque/translucent display-list paths")
        preludes = {key: o2r.safe_archive_path(path) for key, path in preludes.items()}
        room_input = self.layout.get("room_segments", {})
        if not isinstance(room_input, dict) or set(room_input) - set(room_paths):
            raise ValueError("room_segments keys must be resource paths in the selected scene's room table")
        room_segments = {path: parse_segments(bindings) for path, bindings in room_input.items()}
        self.report["pass_preludes"] = preludes
        profile = self.scene_profile()
        trace_report = trace.DrawTracer(self.resources, self.root, segments, profile=profile, draw_sink=self.draw,
                                        pass_preludes=preludes, room_segments=room_segments).run()
        self.report.update(candidate_draws=len(trace_report["draws"]), trace_issues=trace_report["issues"],
                           trace_coverage=trace_report["coverage"], texture_count=len(self.pngs),
                           room_meshes=trace_report["room_meshes"])
        self.report["rendering_approximations"] = [
            {"room": item["room"], "scene_command_offset": item["scene_command_offset"],
             "reason": "type-2 room culling spheres are retained as metadata but not evaluated; all referenced polygon lists are converted in source order",
             "impact": "may submit off-screen geometry and increase draw work; geometry is not transformed by sphere centers"}
            for item in trace_report["room_meshes"] if item["type"] == 2]
        self.report["rendering_approximations"].extend(trace_report.get("visibility_approximations", []))
        self.report["rendering_approximations"].extend(
            {"reason": reason, "draw_count": count}
            for reason, count in sorted(self.accepted_approximation_counts.items()))
        self.report["trace_profile"] = trace_report.get("profile")
        self.converted_sources.update(trace_report["coverage"]["static_matrix_resources"])
        rooms = [{"id": i + 1, "batches": self.batches[path], "neighbors": []} for i, path in enumerate(room_paths)]
        summaries = [{"id": room["id"], "neighbors": room["neighbors"], "batches": len(room["batches"]),
                      "render_triangles": sum(len(batch["render_vertices"]) // 3 for batch in room["batches"]),
                      "collision_triangles": 0} for room in rooms]
        self.report["rooms"] = summaries
        self.report["materials"] = {
            "unique": len({json.dumps(batch["material"], sort_keys=True)
                           for room in rooms for batch in room["batches"]}),
            "opaque_batches": sum(batch["pass"] == "opaque" for room in rooms for batch in room["batches"]),
            "translucent_batches": sum(batch["pass"] == "translucent" for room in rooms for batch in room["batches"]),
        }
        connect_rooms(rooms, command_of(source, 14, self.root).get("transition_actors", []),
                      self.layout.get("links", []))
        if set(trace_report["rooms"]) != set(room_paths) or any(not room["batches"] for room in rooms):
            self.report["scene_issues"].append({"stage": "geometry", "resource": self.root,
                                               "reason": "every room needs a normal mesh with supported triangles"})
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
        for room, summary in zip(rooms, summaries):
            summary["collision_triangles"] = len(room["collision_triangles"])
        apply_bounds(rooms, self.layout.get("bounds"))
        entrances = self.entrances(source, rooms)
        self.report["entrances"] = entrances
        self.report["source_default_entrance"] = self.layout.get("entrance", 0)
        metadata = sorted({f"{path}: {command['name']} metadata has no WSC gameplay mapping"
                           for path in dict.fromkeys([self.root, *room_paths])
                           for command in self.resources.read(path, ("Scene",))
                           if command["id"] not in (0, 3, 4, 6, 8, 10, 14, 20)})
        if commands_of(source, 14):
            metadata.append("Transition actors provide room adjacency only; door actors and camera effects are not instantiated")
        self.report["unmapped_metadata"] = metadata
        if any(self.report[key] for key in ("trace_issues", "draw_issues", "scene_issues")):
            return None
        self.converted_sources.update({self.root, *room_paths})
        all_materials = [batch["material"] for room in rooms for batch in room["batches"]]
        version = (5 if any(material.get("decal") for material in all_materials)
                   else 4 if any(material.get("fog") for material in all_materials) else 3)
        return {"version": version, "id": SCENE_ID, "rooms": rooms, "entrances": entrances}

    def compatibility_summary(self, package=None):
        reasons = Counter(failure["reason"] for failure in self.failures)
        trace_reasons = Counter(issue["reason"] for issue in self.report.get("trace_issues", []))
        translated = max(0, self.report["candidate_draws"] - len(self.failures))
        render_vertices = sum(len(batch["render_vertices"])
                              for batches in self.batches.values() for batch in batches)
        collision_vertices = (sum(len(room["collision_vertices"]) for room in package["rooms"])
                              if package else None)
        collision_triangles = sum(room.get("collision_triangles", 0) for room in self.report.get("rooms", []))
        room_links = sum(len(room.get("neighbors", [])) for room in self.report.get("rooms", [])) // 2
        texture_bytes = sum(width * height * 4 for width, height, _ in self.pngs.values())
        return {
            "schema_version": 1,
            "draws": {"candidate": self.report["candidate_draws"], "translated": translated,
                      "rejected": len(self.failures), "rejection_reasons": dict(sorted(reasons.items()))},
            "geometry": {"rooms": len(self.report.get("rooms", [])), "materials": len({
                json.dumps(batch["material"], sort_keys=True)
                for batches in self.batches.values() for batch in batches}),
                "batches": sum(len(batches) for batches in self.batches.values()),
                "render_triangles": sum(len(batch["render_vertices"]) // 3
                                         for batches in self.batches.values() for batch in batches),
                "static_matrix_resources": self.report.get("trace_coverage", {}).get("static_matrix_resources", [])},
            "textures": {"count": len(self.pngs), "decoded_rgba_bytes": texture_bytes,
                         "formats": ["RGBA16", "RGBA32", "CI4", "CI8", "I4", "I8", "IA4", "IA8", "IA16"],
                         "loadtile": ["4-bit packed crop", "8-bit byte crop", "16-bit byte crop"]},
            "collision": {"vertices": collision_vertices, "triangles": collision_triangles,
                          "coverage": "validated source polygon indices and winding; water boxes, explicit surface mappings, and omitted source corner flags are reported separately",
                          "explicit_approximations": self.report.get("collision_approximations", [])},
            "entrance_approximations": self.report.get("entrance_approximations", []),
            "rooms": {"entrances": len(self.report.get("entrances", [])), "neighbor_links": room_links,
                      "connected_rooms": sum(bool(room.get("neighbors")) for room in self.report.get("rooms", []))},
            "memory_estimate": {"package_bytes": len(package["bytes"]) if package else None,
                                "decoded_texture_rgba_bytes": texture_bytes,
                                "render_vertex_payload_bytes": render_vertices * 14,
                                "collision_vertex_payload_bytes": collision_vertices * 6 if collision_vertices is not None else None,
                                "collision_triangle_payload_bytes": collision_triangles * 8,
                                "method": "serialized WSC payload and decoded RGBA texture bytes; excludes allocator/GPU overhead"},
        "unsupported_features": sorted(set(self.report.get("unmapped_metadata", [])) |
                                            {failure["reason"] for failure in self.failures} |
                                            {issue["reason"] for issue in self.report.get("trace_issues", [])}),
            "rendering_approximations": self.report.get("rendering_approximations", []),
            "trace_issue_reasons": dict(sorted(trace_reasons.items())),
            "supported_subsets": {
                "display_lists": ["bounded F3DEX2 triangle batches", "hashed display-list calls",
                                   "explicit static segment bindings", "hashed/segmented static model-view matrices"],
                "textures": ["whole-image loads", "validated 4/8/16-bit LOADTILE crops", "CI4/CI8 palette baking"],
                "materials": ["validated dual-texture environment-alpha subset", "point/three-point filter",
                              "per-axis repeat/clamp/mirror where WSC3 addressing is equivalent",
                              "WSC4 runtime clip-depth fog", "static RSP vertex lighting baked into shade color"],
                "scene": ["room-local collision", "validated spawn floors", "room adjacency and safe re-entry"]},
        }

    def entrances(self, source, rooms):
        actors = command_of(source, 0, self.root).get("entries", [])
        rows = command_of(source, 6, self.root).get("entrances", [])
        if not rows:
            raise ValueError("source scene has no entrance table")
        excluded_values = self.layout.get("exclude_entrances", [])
        if not isinstance(excluded_values, list) or len(excluded_values) > len(rows):
            raise ValueError("exclude_entrances must be a list of source entrance indices")
        excluded = {wsc.integer(index, 0, len(rows) - 1, "excluded source entrance")
                    for index in excluded_values}
        if len(excluded) != len(excluded_values):
            raise ValueError("exclude_entrances contains duplicate source entrance indices")
        entrances = []
        for index, row in enumerate(rows):
            room_id = wsc.integer(row["room"], 0, len(rooms) - 1, "entrance room")
            actor = actors[wsc.integer(row["spawn"], 0, len(actors) - 1, "entrance spawn")]
            point = self.point(actor["position"])
            room = rooms[room_id]
            position = safe_spawn(room, point)
            if position is None:
                if index in excluded:
                    self.report["entrance_approximations"].append({
                        "source_entrance": index, "source_spawn": row["spawn"],
                        "source_room": row["room"], "position": point,
                        "reason": "source entrance explicitly excluded by layout because no safe local floor exists",
                    })
                    continue
                raise ValueError(f"entrance {index} has no safe local floor near {point}; check the source spawn and collision")
            if index in excluded:
                raise ValueError(f"source entrance {index} has a safe local floor and cannot be excluded")
            entrances.append({"id": index, "room": room["id"], "position": position, "yaw": actor["rotation"][1]})
        if not entrances:
            raise ValueError("all source entrances were excluded; at least one safe entrance is required")
        selected = wsc.integer(self.layout.get("entrance", 0), 0, len(rows) - 1, "default entrance")
        selected_index = next((i for i, row in enumerate(entrances) if row["id"] == selected), None)
        if selected_index is None:
            raise ValueError(f"default source entrance {selected} is excluded or has no safe local floor")
        old_selected_id = entrances[selected_index]["id"]
        zero_index = next((i for i, row in enumerate(entrances) if row["id"] == 0), None)
        if zero_index is not None:
            entrances[zero_index]["id"] = old_selected_id
        entrances[selected_index]["id"] = 0
        return entrances


def cross(a, b, c):
    u, v = [x - y for x, y in zip(b, a)], [x - y for x, y in zip(c, a)]
    return [u[(i + 1) % 3] * v[(i + 2) % 3] - u[(i + 2) % 3] * v[(i + 1) % 3] for i in range(3)]


def transform_position(position, matrix):
    if any(abs(matrix[row][3]) > 1e-9 for row in range(3)) or abs(matrix[3][3] - 1.0) > 1e-9:
        raise ValueError("projective or non-affine model-view matrix cannot be baked into world coordinates")
    return [sum(position[row] * matrix[row][column] for row in range(3)) + matrix[3][column]
            for column in range(3)]


def rsp_vertex_lighting(normal, modelview, lights, directional_count):
    """Bake the pinned gfx_pc RSP shade equation from static Light_t records."""
    if not 0 <= directional_count <= 7:
        raise ValueError("RSP directional light count exceeds the seven-light limit")
    required = {str(index) for index in range(directional_count + 1)}
    if not required.issubset(lights):
        raise ValueError("lighting draw is missing a directional or ambient Light_t record")
    result = list(lights[str(directional_count)]["color"])
    for index in range(directional_count):
        record = lights[str(index)]
        direction = [component / 127.0 for component in record["direction"]]
        transformed = [sum(direction[row] * modelview[row][column] for row in range(3))
                       for column in range(3)]
        magnitude = sum(component * component for component in transformed) ** 0.5
        if magnitude < 1e-12:
            raise ValueError(f"directional light {index} has a zero transformed direction")
        transformed = [component / magnitude for component in transformed]
        intensity = sum(normal[axis] * transformed[axis] for axis in range(3)) / 127.0
        if intensity > 0:
            result = [component + intensity * color for component, color in zip(result, record["color"])]
    return [max(0, min(255, int(component))) for component in result]


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


def collision_flag_policy(layout):
    values = layout.get("ignore_collision_vertex_flags", [])
    if not isinstance(values, list) or len(values) > 64:
        raise ValueError("ignore_collision_vertex_flags must be a list of at most 64 flag pairs")
    result = set()
    for pair in values:
        if not isinstance(pair, list) or len(pair) != 2:
            raise ValueError("each ignored collision flag entry must contain two vertex flag values")
        flags = tuple(wsc.integer(value, 0, 7, "source collision vertex flag") for value in pair)
        if flags == (0, 0) or flags in result:
            raise ValueError("ignored collision flag pairs must be distinct and nonzero")
        result.add(flags)
    return result


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


def parse_segments(bindings):
    if not isinstance(bindings, list) or len(bindings) > 16:
        raise ValueError("segment bindings require a list of up to 16 HEX[@OFFSET]=RESOURCE entries")
    segments = {}
    for value in bindings:
        descriptor, path = value.split("=", 1)
        address, separator, offset = descriptor.partition("@")
        number = int(address, 16)
        base = int(offset, 16) if separator else 0
        if number in segments or not 0 <= number < 16 or not 0 <= base <= 0xFFFFFE or base & 1:
            raise ValueError(f"duplicate or invalid segment binding: {descriptor}")
        segments[number] = trace.Reference(o2r.safe_archive_path(path), base)
    return segments


CONVERSION_ERRORS = (OSError, BadZipFile, KeyError, TypeError, ValueError, ZeroDivisionError,
                     struct.error, o2r.ImportFailure, trace.TraceError)


def convert_archive(archive_path, source, layout, scale, offset, expanded, segments, *, audit=False):
    if archive_path.stat().st_size > trace.MAX_ARCHIVE_FILE_BYTES:
        raise ValueError("archive file exceeds the O2R size limit")
    if scale <= 0 or scale > 1024:
        raise ValueError("--scale must be positive and no greater than 1024")
    allowed = {"bounds", "collision_rooms", "links", "surface_map", "entrance", "pass_preludes", "room_segments",
               "hyrule_context", "ignore_water_boxes", "ignore_collision_vertex_flags", "exclude_entrances"}
    if not isinstance(layout, dict) or set(layout) - allowed:
        raise ValueError("invalid layout fields")
    with ZipFile(archive_path) as archive:
        resources = trace.ArchiveResources(archive)
        conversion = Conversion(resources, o2r.safe_archive_path(source), scale, offset, layout, expanded)
        stage = "scene, collision and entrances"
        package = None
        try:
            scene = conversion.convert(segments)
            if scene is not None:
                stage = "WSC3 package bounds"
                package = wsc.encode_scene(scene)
                conversion.report.update(status="converted", package_bytes=len(package),
                                         package_format=f"WSC{scene['version']}")
        except CONVERSION_ERRORS as error:
            if not audit:
                raise
            conversion.report["scene_issues"].append({"stage": stage, "resource": source, "reason": str(error)})
        conversion.report["compatibility"] = conversion.compatibility_summary(
            {"rooms": scene["rooms"], "entrances": scene["entrances"], "bytes": package}
            if package is not None else None)
        conversion.report["resources"] = {
            "archive_entries": len(resources.index),
            "observed": [{"path": path, **record} for path, record in sorted(resources.accessed.items())],
            "converted": sorted(conversion.converted_sources),
        }
        return package, conversion.pngs, conversion.report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("scene", help="exact scene resource path inside the archive")
    parser.add_argument("output", type=Path, nargs="?", help="new directory to stage for copying into app Documents")
    parser.add_argument("--dry-run", action="store_true", help="print compatibility JSON to stdout without staging files")
    parser.add_argument("--layout", type=Path, help="optional scene-independent JSON ownership and room metadata")
    parser.add_argument("--scale", type=Fraction, default=Fraction(1), help="units multiplier, e.g. 1/2")
    parser.add_argument("--offset", type=int, nargs=3, default=(0, 0, 0), metavar=("X", "Y", "Z"))
    parser.add_argument("--expanded-grid", action="store_true", help="target a build with the expanded collision grid")
    parser.add_argument("--segment", action="append", default=[], metavar="HEX[@OFFSET]=RESOURCE")
    args = parser.parse_args(argv)
    if args.dry_run == (args.output is not None):
        parser.error("provide either an output directory or --dry-run")
    try:
        if args.output and (args.output.exists() or args.output.is_symlink()):
            raise ValueError("output must be a new directory")
        layout = {}
        if args.layout:
            if args.layout.stat().st_size > MAX_LAYOUT_BYTES:
                raise ValueError("layout exceeds byte limit")
            layout = json.loads(args.layout.read_text(encoding="utf-8"))
        segments = parse_segments(args.segment)
        package, pngs, report = convert_archive(args.archive, args.scene, layout, args.scale,
                                               args.offset, args.expanded_grid, segments, audit=True)
        if args.dry_run:
            report["status"] = "compatible" if package is not None else "rejected"
        encoded_report = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.dry_run:
            sys.stdout.write(encoded_report)
            return 0 if package is not None else 2
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
    except CONVERSION_ERRORS as error:
        if args.dry_run:
            print(json.dumps({"schema_version": 1, "status": "rejected", "source_scene": args.scene,
                              "scene_issues": [{"stage": "input", "resource": args.scene, "reason": str(error)}]},
                             indent=2, sort_keys=True))
        else:
            print(f"o2r-to-wsc3: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
