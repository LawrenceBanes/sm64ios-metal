"""Sourced, host-only Fast3D pass prelude and generated segment packets."""

from dataclasses import dataclass
import struct


OOT_PIN = "https://github.com/zeldaret/oot/blob/14759abf9abb4295de142cdc1452998630ea70fb/"
ROOM_SETUP = OOT_PIN + "src/code/z_rcp.c#L298-L308"
OPAQUE_DITHER = OOT_PIN + "src/code/z_rcp.c#L1055-L1067"
ROOM_DRAW = OOT_PIN + "src/code/z_room.c#L76-L113"
SCENE_CONFIG = OOT_PIN + "src/code/z_scene_table.c#L1176-L1232"
SCROLL_SOURCE = OOT_PIN + "src/code/z_rcp.c#L1450-L1466"
SCENE_ORDER = OOT_PIN + "src/code/z_play.c#L1330-L1343"
HYRULE_SCENE = "scenes/shared/spot00_scene/spot00_scene"
HYRULE_CONFIG = OOT_PIN + "src/code/z_scene_table.c#L990-L1030"
HYRULE_LIGHTING = OOT_PIN + "src/code/z_play.c#L243-L249"
FOG_RANGE = OOT_PIN + "src/code/z_rcp.c#L862-L895"
FOG_POSITION = OOT_PIN + "include/ultra64/gbi.h#L2750-L2763"
KOKIRI_SCENE = "scenes/shared/spot04_scene/spot04_scene"
NTSC_10 = bytes.fromhex("01ec7011b7")


@dataclass(frozen=True)
class GeneratedList:
    source: str
    packets: tuple[tuple[int, int], ...]
    helper: str | None = None

    def body(self):
        return b"\x04" + bytes(7) + b"".join(struct.pack(">II", *p) for p in self.packets)


@dataclass(frozen=True)
class PassPrelude:
    bindings: dict[int, GeneratedList]
    commands: tuple[GeneratedList, ...]


def env_color(red, green, blue, alpha, source):
    channels = (red, green, blue, alpha)
    if any(type(c) is not int or not 0 <= c <= 255 for c in channels):
        raise ValueError("invalid environment color")
    return GeneratedList(source, ((0xE7000000, 0), (0xFB000000, int.from_bytes(bytes(channels), "big")),
                                   (0xDF000000, 0)))


def f32(value):
    return struct.unpack(">f", struct.pack(">f", value))[0]


def tile_scroll(tiles, source):
    """Build one or two 10.2 tile-origin updates using Gfx_TwoTexScroll arithmetic."""
    if not 1 <= len(tiles) <= 2 or len({tile[0] for tile in tiles}) != len(tiles):
        raise ValueError("scroll must address one or two distinct tiles")
    packets = []
    for tile, s, t, width, height in tiles:
        if not 0 <= tile < 8 or not 1 <= width <= 512 or not 1 <= height <= 512:
            raise ValueError("invalid scroll tile or extent")
        # Upstream casts the origin to u32, takes % 2048, and then packs
        # 12-bit upper and lower coordinates. 2048 divides the u32 range.
        s, t = s % 2048, t % 2048
        right, bottom = (s + 4 * (width - 1)) & 0xFFF, (t + 4 * (height - 1)) & 0xFFF
        packets.extend(((0xE8000000, 0), (0xF2000000 | s << 12 | t, tile << 24 | right << 12 | bottom)))
    packets.append((0xDF000000, 0))
    return GeneratedList(source, tuple(packets), SCROLL_SOURCE)


# SETUPDL_25 from GBI macro expansion. Both passes use this list.
# Texture enabled, tile 0, no LOD, full scales. MODULATEIDECALA then
# MODULATEIA_PRIM2. Perspective, bilerp, no TLUT, two cycles, pixel Z,
# fog/shade in blend cycle one, AA Z-buffered opaque in cycle two.
ROOM_SETUP_25 = GeneratedList(ROOM_SETUP, (
    (0xE7000000, 0),
    (0xD7000002, 0xFFFFFFFF),
    (0xFC127E03, 0xFF0FF3FF),
    (0xEF182C10, 0xC8112078),
    (0xD9000000, 0x00230405),
    (0xDF000000, 0),
))


@dataclass(frozen=True)
class KokiriInputs:
    frame: int
    layer: int
    event07: bool
    draw_param0: int

    def __post_init__(self):
        if (type(self.frame) is not int or not 0 <= self.frame <= 0xFFFFFFFF or
                type(self.layer) is not int or not 0 <= self.layer <= 19 or
                type(self.event07) is not bool or
                type(self.draw_param0) is not int or not -32768 <= self.draw_param0 <= 32767):
            raise ValueError("Kokiri context is outside its supported input ranges")

    def prelude(self, pass_name, room_type):
        frame = self.frame
        bindings = {}
        if pass_name == "translucent":
            for segment, rate in ((0x08, 10), (0x09, 1)):
                t = frame * rate % 128
                bindings[segment] = tile_scroll(((0, 127 - frame % 128, t, 32, 32),
                                                  (1, frame % 128, t, 32, 32)), SCENE_CONFIG)
        elif pass_name == "opaque":
            alpha = 255 - (self.draw_param0 & 255) if self.layer == 4 else 128
            bindings[0x0A] = env_color(128, 128, 128, alpha, SCENE_CONFIG)
            t = int(f32(-self.draw_param0 * f32(0.02)))
            bindings[0x0C] = tile_scroll(((0, 0, t, 32, 16), (1, 0, t, 32, 16)), SCENE_CONFIG)
        else:
            raise ValueError("unknown room pass")
        strength = 500
        if self.layer == 6:
            strength = (self.draw_param0 + 500) & 0xFFFF
        elif self.layer < 4 and self.event07:
            strength = 2150
        bindings[0x0B] = env_color(128, 128, 128, int(f32(strength * f32(0.1))) & 255, SCENE_CONFIG)
        commands = (env_color(128, 128, 128, 128, SCENE_CONFIG), ROOM_SETUP_25)
        if pass_name == "opaque" and room_type == 3:
            commands += (GeneratedList(OPAQUE_DITHER, ((0xE3001801, 0xC0), (0xDF000000, 0))),)
        return PassPrelude(bindings, commands)


def fog_factor_for_setting(setting):
    """Reproduce pinned OoT Gfx_SetFog for one explicitly selected setting."""
    near, far = setting["fog_near"], setting["fog_far"]
    if far == near:
        far += 1
    if near >= 1000:
        multiplier, offset = 0, 0
    elif near > 996:
        multiplier, offset = 0x7FFF, -0x7F00
    elif near < 0:
        multiplier, offset = 0, 255
    else:
        span = far - near
        if span <= 0:
            raise ValueError("selected lighting setting has an invalid fog range")

        def trunc_div(numerator, denominator):
            return (abs(numerator) // denominator) * (-1 if numerator < 0 else 1)

        multiplier = trunc_div(128000, span)
        offset = trunc_div((500 - near) * 256, span)
        if not -32768 <= multiplier <= 32767 or not -32768 <= offset <= 32767:
            raise ValueError("selected fog range overflows the source signed fog coefficients")
    return {"multiplier": multiplier, "offset_coefficient": offset,
            "origin": "reconstructed", "source": FOG_RANGE,
            "inputs": {"fog_near": setting["fog_near"], "fog_far": setting["fog_far"]}}


@dataclass(frozen=True)
class HyruleFieldInputs:
    """A bounded, explicit NTSC 1.0 Hyrule daytime draw snapshot.

    This does not guess which environment row is active in a save. The caller
    must choose a source lighting row. Night transitions are not reconstructed.
    """
    frame: int
    lighting_setting: int
    daytime: bool

    scene = HYRULE_SCENE
    marker = NTSC_10
    name = "oot-ntsc10-hyrule-field-explicit-daylight-snapshot"

    def __post_init__(self):
        if (type(self.frame) is not int or not 0 <= self.frame <= 0xFFFFFFFF or
                type(self.lighting_setting) is not int or not 0 <= self.lighting_setting < 17 or
                type(self.daytime) is not bool):
            raise ValueError("Hyrule Field context requires frame 0..UINT32_MAX, lighting row 0..16, and a boolean daytime branch")
        if not self.daytime:
            raise ValueError("Hyrule nighttime segment 0x0A depends on runtime drawParams and a conditional room list; only the daytime branch is reconstructed")

    def lighting(self, scene_commands):
        entries = [command.get("lighting_settings", []) for command in scene_commands if command["id"] == 15]
        if len(entries) != 1 or self.lighting_setting >= len(entries[0]):
            raise ValueError("Hyrule Field context requires the selected lighting row from exactly one decoded scene lighting table")
        setting = entries[0][self.lighting_setting]
        def signed_direction(values):
            return [value - 256 if value >= 128 else value for value in values]
        lights = {
            "0": {"color": setting["light1_color"], "direction": signed_direction(setting["light1_direction"])},
            "1": {"color": setting["light2_color"], "direction": signed_direction(setting["light2_direction"])},
            "2": {"color": setting["ambient"], "direction": [0, 0, 0]},
        }
        provenance = {"origin": "reconstructed", "source": HYRULE_LIGHTING,
                      "lighting_setting_index": self.lighting_setting}
        fog = fog_factor_for_setting(setting)
        fog_color = {"rgba": [*setting["fog_color"], 0], **provenance}
        return {"setting": setting, "lights": lights, "provenance": provenance,
                "fog_factor": fog, "fog_color": fog_color}

    def seed(self, state, scene_commands):
        selected = self.lighting(scene_commands)
        state.num_directional_lights = {"count": 2, **selected["provenance"]}
        state.lights = selected["lights"]
        state.fog_factor = selected["fog_factor"]
        state.colors["fog"] = selected["fog_color"]

    def prelude(self, pass_name, room_type):
        if pass_name not in ("opaque", "translucent"):
            raise ValueError("unknown room pass")
        bindings = {}
        if pass_name == "translucent":
            for segment, rate in ((0x08, 3), (0x09, 10)):
                t = self.frame * rate % 128
                bindings[segment] = tile_scroll(((0, 127 - self.frame % 128, t, 32, 32),
                                                  (1, self.frame % 128, t, 32, 32)), HYRULE_CONFIG)
            # In the source daytime branch, 0x0A points to a one-command
            # EndDisplayList. The night branch has conditional alpha and an
            # additional room list, so it is intentionally outside this profile.
            bindings[0x0A] = GeneratedList(HYRULE_CONFIG, ((0xDF000000, 0),))
        commands = (env_color(128, 128, 128, 128, HYRULE_CONFIG), ROOM_SETUP_25)
        return PassPrelude(bindings, commands)

    def metadata(self):
        return {"name": self.name,
                "inputs": {"frame": self.frame, "lighting_setting": self.lighting_setting,
                           "daytime": self.daytime},
                "sources": [HYRULE_CONFIG, HYRULE_LIGHTING, FOG_RANGE, FOG_POSITION, ROOM_DRAW],
                "scope": "explicit scene lighting-row snapshot and daytime segment branch; type-2 visibility and camera-dependent translucent sorting are not evaluated"}


@dataclass(frozen=True)
class StaticSceneLightingInputs:
    """Explicit NTSC 1.0 static-scene lighting and common room-pass setup.

    This profile is for scenes whose room lists have no runtime-generated
    material segments. It does not infer day/night selection or resolve
    scene-specific animated commands; the caller chooses a lighting row and
    environment color explicitly.
    """
    scene: str
    lighting_setting: int
    environment_rgba: tuple[int, int, int, int] = (128, 128, 128, 128)
    marker = NTSC_10
    name = "oot-ntsc10-explicit-static-scene-lighting-snapshot"

    def __post_init__(self):
        if (not isinstance(self.scene, str) or not self.scene.startswith("scenes/")
                or type(self.lighting_setting) is not int or not 0 <= self.lighting_setting < 256
                or len(self.environment_rgba) != 4
                or any(type(value) is not int or not 0 <= value <= 255
                       for value in self.environment_rgba)):
            raise ValueError("static scene context requires a scene path, lighting row and RGBA environment color")

    def lighting(self, scene_commands):
        entries = [command.get("lighting_settings", []) for command in scene_commands
                   if command["id"] == 15]
        if len(entries) != 1 or self.lighting_setting >= len(entries[0]):
            raise ValueError("static scene context requires the selected row from exactly one decoded lighting table")
        setting = entries[0][self.lighting_setting]
        signed_direction = lambda values: [value - 256 if value >= 128 else value for value in values]
        provenance = {"origin": "reconstructed", "source": SCENE_ORDER,
                      "lighting_setting_index": self.lighting_setting}
        lights = {
            "0": {"color": setting["light1_color"],
                  "direction": signed_direction(setting["light1_direction"])},
            "1": {"color": setting["light2_color"],
                  "direction": signed_direction(setting["light2_direction"])},
            "2": {"color": setting["ambient"], "direction": [0, 0, 0]},
        }
        fog = fog_factor_for_setting(setting)
        fog_color = {"rgba": [*setting["fog_color"], 0], **provenance}
        return {"setting": setting, "lights": lights, "provenance": provenance,
                "fog_factor": fog, "fog_color": fog_color}

    def seed(self, state, scene_commands):
        selected = self.lighting(scene_commands)
        state.num_directional_lights = {"count": 2, **selected["provenance"]}
        state.lights = selected["lights"]
        state.fog_factor = selected["fog_factor"]
        state.colors["fog"] = selected["fog_color"]

    def prelude(self, pass_name, room_type):
        if pass_name not in ("opaque", "translucent"):
            raise ValueError("unknown room pass")
        source = "explicit static-scene pass configuration"
        commands = (env_color(*self.environment_rgba, source), ROOM_SETUP_25)
        if pass_name == "opaque" and room_type == 3:
            commands += (GeneratedList(OPAQUE_DITHER, ((0xE3001801, 0xC0),
                                                       (0xDF000000, 0))),)
        return PassPrelude({}, commands)

    def metadata(self):
        return {"name": self.name,
                "inputs": {"lighting_setting": self.lighting_setting,
                           "environment_rgba": list(self.environment_rgba)},
                "sources": [SCENE_ORDER, ROOM_SETUP, ROOM_DRAW, FOG_RANGE, FOG_POSITION],
                "scope": "explicit static NTSC 1.0 lighting-row snapshot and common room setup; dynamic scene segments and time selection are not inferred"}
