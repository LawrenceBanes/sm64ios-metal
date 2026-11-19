"""Symbolic material inputs for the pinned Fast3D triangle shader subset."""

# G_SETCOMBINE selectors are slot-specific. The second cycle switches
# TEXEL0/TEXEL1 and one-cycle TEXEL1 aliases TEXEL0 in the pinned interpreter.
def selector_name(channel, slot, value, index, two_cycle):
    kind = "a" if channel == "alpha" else "rgb"
    if value in (1, 2):
        texture = value - 1
        if index:
            texture ^= 1
        return f"tex{texture if two_cycle else 0}.{kind}"
    if not value:
        if channel == "alpha" and slot == 2:
            return "lod_fraction"
        return f"combined.{kind}"
    if value in (3, 4, 5):
        return f"{('primitive', 'shade', 'environment')[value - 3]}.{kind}"
    if channel == "alpha":
        return ("primitive_lod_fraction" if slot == 2 else 1) if value == 6 else 0
    if slot == 0:
        return {6: 1, 7: "noise"}.get(value, 0)
    if slot == 1:
        return {6: "key_center", 7: "convert_k4"}.get(value, 0)
    if slot == 3:
        return 1 if value == 6 else 0
    if value in (8, 9):
        texture = (value - 8) ^ index
        return f"tex{texture if two_cycle else 0}.a"
    return {6: "key_scale", 7: "combined.a", 10: "primitive.a", 11: "shade.a",
            12: "environment.a", 13: "lod_fraction", 14: "primitive_lod_fraction",
            15: "convert_k5"}.get(value, 0)


def equation(raw, channel, index, two_cycle, combined):
    a, b, c, d = [selector_name(channel, slot, val, index, two_cycle) for slot, val in enumerate(raw)]
    # The pinned GenerateCC normalizes equal encoded A/B selectors before
    # translating the overloaded slot meanings for chroma key and convert.
    if raw[0] == raw[1] or a == b or c == 0:
        return combined.get(d, d)
    a, b, c, d = (combined.get(value, value) for value in (a, b, c, d))
    if b == 0 and d == 0:
        if a == 1:
            return c
        if c == 1:
            return a
        return ("multiply", *sorted((a, c), key=repr))
    return ("combine", a, b, c, d)


def expression_inputs(expr):
    if isinstance(expr, tuple):
        return set().union(*(expression_inputs(child) for child in expr[1:]))
    return {expr} if isinstance(expr, str) else set()


def material_expression(cycles, cycle_type, alpha_used):
    if cycle_type not in (0, 1) or not cycles or alpha_used is None:
        return None
    combined = {}
    for i, cycle in enumerate(cycles[:cycle_type + 1]):
        rgb = equation(cycle["rgb"], "rgb", i, bool(cycle_type), combined)
        alpha = equation(cycle["alpha"], "alpha", i, bool(cycle_type), combined)
        combined = {"combined.rgb": rgb, "combined.a": alpha}
    channels = expression_inputs(rgb) | (expression_inputs(alpha) if alpha_used else set())
    return {"rgb": rgb, "alpha": alpha, "inputs": sorted(channels),
            "texture_inputs": sorted({int(name[3]) for name in channels if name.startswith("tex")})}


def reference_blender(low):
    """Classify fixed Fast3D shader switches, retaining raw blender words separately."""
    alpha = any((low >> mem & 3, low >> inv & 3) == (1, 0) for mem, inv in ((20, 16), (22, 18)))
    coverage = bool(low & 0x1000)
    source = low >> 30
    return {"alpha": alpha or coverage, "fog": source == 3, "blend_color": source == 2,
            "alpha_test": bool(low & 3) or coverage, "coverage_alpha": coverage,
            "invisible": low >> 24 & 3 == 3 and low >> 20 & 3 == 1}
