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
