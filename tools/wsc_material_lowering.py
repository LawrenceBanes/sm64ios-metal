"""Evaluate Fast3D draw metadata against WSC2 without emitting a scene package."""

from collections import Counter
from fractions import Fraction

if __package__:
    from .world_scene_package import decode_indexed_rgba, texture_descriptor
else:
    from world_scene_package import decode_indexed_rgba, texture_descriptor


EXACT = "exact"
BAKED = "validated_baking"
APPROXIMATE = "documented_approximation"
EXTENSION = "requires_versioned_extension"
BLOCKED = "blocked_by_unresolved_source_state"


def bake_indexed(packed, tlut, *, width, height, index_bits, bank, interpretation):
    """Validate authored contiguous CI input and bake the selected TLUT bank."""
    if index_bits not in (4, 8) or not 0 <= bank < (16 if index_bits == 4 else 1):
        raise ValueError("invalid index size or palette bank")
    if len(tlut) > 512 or len(tlut) % 2:
        raise ValueError("invalid full palette length")
    size = (1 << index_bits) * 2
    first = bank * size
    colors = tlut[first:first + size]
    if len(colors) != size:
        raise ValueError("palette bank is incomplete")
    return decode_indexed_rgba({"name": "scenes/fixture-texture.rgba16", "width": width,
                                "height": height, "index_bits": index_bits, "indices": packed.hex(),
                                "palette": colors.hex(), "palette_format": interpretation})


def bake_uv(value, scale, shift, origin):
    """Find the WSC signed 10.5 input that gives the same static loaded UV."""
    if not (-32768 <= value <= 32767 and 0 <= scale <= 65535 and
            0 <= shift <= 15 and 0 <= origin <= 4095):
        raise ValueError("source UV or tile transform out of bounds")
    multiplier = Fraction(1, 1 << shift) if shift <= 10 else Fraction(1 << (16 - shift))
    result = Fraction(value * scale >> 16) * multiplier - 8 * origin
    if result.denominator != 1 or not -32768 <= result <= 32766:
        raise ValueError("static UV does not fit WSC2 signed 10.5 precision")
    loaded = int(result)
    return loaded + (loaded >= 0)


def scroll_rate(origins_10_2, dimension):
    """Return a single WSC scroll rate if a full repeating source cycle proves it."""
    if not 2 <= len(origins_10_2) <= 2048 or not 1 <= dimension <= 2048:
        raise ValueError("invalid scroll period or texture dimension")
    if any(not 0 <= origin <= 4095 for origin in origins_10_2):
        raise ValueError("scroll origin is outside the 10.2 tile field")
    period = 32 * dimension
    base = -8 * origins_10_2[0]
    step = -8 * origins_10_2[1] - base
    if (len(origins_10_2) * step % period or
            any((-8 * origin - base - i * step) % period for i, origin in enumerate(origins_10_2))):
        return None
    return (step + period // 2) % period - period // 2


def remove_identity_multipliers(expression, colors):
    if not isinstance(expression, (list, tuple)) or expression[0] != "multiply":
        return expression
    operands = [remove_identity_multipliers(operand, colors) for operand in expression[1:]]
    for index, operand in enumerate(operands):
        if not isinstance(operand, str) or "." not in operand:
            continue
        source, channel = operand.split(".", 1)
        color = colors.get(source)
        if color and all(value == 255 for value in (color["rgba"][:3] if channel == "rgb" else color["rgba"][3:])):
            return operands[1 - index]
    return ("multiply", *operands)


def classify_draw(draw, *, inherited_filter=None, validated_bakes=()):
    """Generate a feasibility verdict. This takes metadata and optional authored proofs."""
    missing = set(draw["missing"])
    limits = set()
    approximations = set(draw["approximations"])
    pending = set()
    proofs = set(validated_bakes)
    state = draw["render_state"]
    material = draw["combiner"]["effective"]
    blender = state["reference_blender"]
    translucent = draw["pass"] == "translucent"
    if material is None or blender is None:
        missing.add("effective combiner or blender unavailable")
    else:
        inputs = material["inputs"]
        used = material["texture_inputs"]
        if set(used) != {texture["input"] for texture in draw["textures"]}:
            missing.add("effective combiner bindings were not resolved with the selected trace profile")
        colors = draw["combiner"]["colors"]
        rgb = remove_identity_multipliers(material["rgb"], colors)
        alpha = remove_identity_multipliers(material["alpha"], colors)
        modulate = ("multiply", "shade.rgb", f"tex{used[0]}.rgb") if len(used) == 1 else None
        expected_rgb = modulate if used else "shade.rgb"
        expected_alpha = ("multiply", "shade.a", f"tex{used[0]}.a") if len(used) == 1 else "shade.a"
        if len(used) > 1:
            limits.add("two independently sampled textures")
        if (tuple(rgb) if isinstance(rgb, list) else rgb) != expected_rgb:
            limits.add("RGB combiner differs from WSC2 shade or texture modulation")
        if blender["alpha"] and (tuple(alpha) if isinstance(alpha, list) else alpha) != expected_alpha:
            limits.add("alpha combiner differs from WSC2 shade or texture modulation")
        if any(name.startswith("environment.") for name in inputs):
            limits.add("effective environment color input")
        if any(name.startswith("primitive") for name in inputs):
            pending.add("prove any primitive modulation is representable by WSC2 tint or texture baking")
        if blender["fog"] or blender["blend_color"]:
            limits.add("fog or blend-color input")
        if blender["alpha_test"]:
            limits.add("alpha comparison or coverage")
        if blender["invisible"]:
            limits.add("invisible blender mode")
        if blender["alpha"] != translucent:
            limits.add("blend enable differs from WSC2 pass")
        low = state["other_mode_low"]
        if (state["depth_test"], state["depth_write"], state["z_mode"], bool(low & 4)) != (
                True, not translucent, 2 if translucent else 0, False):
            limits.add("depth test, write, source or Z mode differs from WSC2 pass")

    # WSC2 clears the source geometry modes and supplies straight vertex colors.
    geometry_bits = 0x0F0600
    if draw["geometry"]["known_mask"] & geometry_bits != geometry_bits:
        missing.add("source culling, lighting, fog or texgen flags unknown")
    if draw["geometry"]["mode"] & 0x600:
        limits.add("face culling differs from WSC2")
    if draw["geometry"]["mode"] & 0x20000:
        limits.add("lighting differs from WSC2 vertex colors")
    if draw["geometry"]["mode"] & 0xC0000:
        limits.add("generated texture coordinates")
    if any(source.get("geometry_at_vertex_load", 0) & 0xE0000 for source in draw["geometry"]["vertex_ranges"]):
        limits.add("vertex-load lighting or generated texture coordinates")

    for texture in draw["textures"]:
        if "uv" not in texture:
            missing.add("texture binding or dimensions unresolved")
            continue
        uv = texture["uv"]
        load = texture["load"]
        image = load["image"]["source"]
        width, height = image["width"], image["height"]
        try:
            texture_descriptor({"name": "scenes/fixture-texture.rgba16", "width": width, "height": height})
        except ValueError:
            limits.add("texture extent exceeds WSC2 material budget")
        high = state["other_mode_high"]
        filter_mode = (high >> 12) & 3
        if filter_mode not in (0, 2) or high & 0x70000:
            limits.add("texture filter or LOD mode outside WSC2")
        elif inherited_filter != filter_mode:
            pending.add("WSC2 inherits filter mode")
        if not high & 0x80000:
            limits.add("non-perspective texture sampling")
        if [width, height] != uv["tile_extent"] or load["kind"] != "block" or load["source_offset"]:
            pending.add("validate TMEM footprint against the backing texture")
        if any(uv[axis]["clamp"] or uv[axis]["mirror"] or uv[axis]["mask_period_texels"] != uv[axis]["normalization_dimension"]
               for axis in ("s", "t")):
            limits.add("sampler clamp, mirror or mask period differs from WSC2 repeat")
        if any(uv[axis]["shift_raw"] or uv[axis]["origin_10_2"] for axis in ("s", "t")):
            bounds_src = texture["descriptor"].get("bounds_source", {})
            if bounds_src.get("helper") and "scroll" not in proofs:
                pending.add("prove generated motion against a single WSC2 scroll slot and its clock")
            elif "uv" not in proofs and "scroll" not in proofs:
                pending.add("prove transformed UV precision and signed range")
        expected_bytes = (width * height * image["bits_per_pixel"] + 7) // 8
        if (load.get("total_bytes") != expected_bytes or
                (image["horizontal_byte_scale"], image["vertical_pixel_scale"]) != (1.0, 1.0)):
            pending.add("validate upload bytes and source extent")
        if "palette" in texture and "palette" not in proofs:
            pending.add("validate CI palette baking against the loaded banks and texture bytes")
        elif "palette" not in texture and (texture["descriptor"]["format"], texture["descriptor"]["size"]) not in ((0, 2), (0, 3)):
            pending.add("validate source texture format conversion")

    if missing:
        category = BLOCKED
    elif limits:
        category = EXTENSION
    elif pending or approximations:
        category = APPROXIMATE
    elif proofs:
        category = BAKED
    else:
        category = EXACT
    return {"classification": category, "known_mismatches": sorted(limits), "unresolved": sorted(missing),
            "candidate_bakes_or_checks": sorted(pending), "approximations": sorted(approximations),
            "validated_bakes": sorted(proofs)}


def lower_report(report, inherited_filter=None):
    for draw in report["draws"]:
        draw["wsc2"] = classify_draw(draw, inherited_filter=inherited_filter)
    summary = Counter(draw["wsc2"]["classification"] for draw in report["draws"])
    report["wsc2"] = {
        "basis": "WSC2 material feasibility per candidate draw, without exporting payloads or validating room budget",
        "inherited_filter": inherited_filter,
        "classifications": {key: summary[key] for key in (EXACT, BAKED, APPROXIMATE, EXTENSION, BLOCKED)},
        "known_mismatches": dict(sorted(Counter(value for draw in report["draws"]
                                                for value in draw["wsc2"]["known_mismatches"]).items())),
        "pending_checks": dict(sorted(Counter(value for draw in report["draws"]
                                               for value in draw["wsc2"]["candidate_bakes_or_checks"]).items())),
    }
