#!/usr/bin/env python3
"""Generate the original WSC2 and WSC3 diagnostic rooms and its private-build pack manifest."""
import argparse
from pathlib import Path
import re

if __package__:
    from .world_scene_package import encode_scene, indexed_textures, write_rgba_png
else:
    from world_scene_package import encode_scene, indexed_textures, write_rgba_png


def build_config():
    header = Path(__file__).resolve().parents[1] / "include/scene_diagnostic.h"
    config = dict(re.findall(r'^#define SCENE_DIAGNOSTIC_(\w+) (.+)$', header.read_text(), re.M))
    return int(config["PACKAGE"]), int(config["WSC3_PACKAGE"]), int(config["INVALID_PACKAGE"]), config["DIRECTORY"].strip('"')


def quad(points, color=(255, 255, 255, 255), uv_max=(0, 0)):
    u, v = uv_max
    vertices = [dict(position=list(point), uv=list(uv), color=list(color))
                for point, uv in zip(points, ((0, v), (0, 0), (u, 0), (u, v)))]
    return [vertices[i] for i in (0, 1, 2, 0, 2, 3)]


def panel(left, right, bottom, top, z, **kwargs):
    return quad([(left, bottom, z), (left, top, z), (right, top, z), (right, bottom, z)], **kwargs)


def indexed(name, bits, palette, index_at):
    indices = [index_at(x, y) for y in range(16) for x in range(16)]
    if bits == 4:
        indices = [(a << 4) | b for a, b in zip(indices[::2], indices[1::2])]
    return dict(name=f"scenes/67f-{name}.rgba16", width=16, height=16,
                index_bits=bits, indices=bytes(indices).hex(), palette=palette[1], palette_format=palette[0])


def authored_scene(scene_id):
    # The left panel is a four-color checker. The right uses CI8 indices
    # above 15 and varies IA88 alpha, over a warm opaque background.
    ci4 = indexed("ci4", 4, ("rgba5551", "001f07ff2a7fffff"),
                  lambda x, y: (x // 4 + 2 * (y // 4)) % 4)
    ci8 = indexed("ci8", 8, ("ia88", "4000" * 16 + "ff20ff78ffc8ffef"),
                  lambda x, y: 16 + ((x + y) // 2) % 4)
    textures = [{k: image[k] for k in ("name", "width", "height")} for image in (ci4, ci8)]
    vertices, triangles, opaque = [], [], []

    def walkable(points, color):
        i = len(vertices)
        vertices.extend(map(list, points))
        triangles.extend([{"indices": [i, i + 1, i + 2]}, {"indices": [i, i + 2, i + 3]}])
        opaque.extend(quad(points, color))

    # The same coordinates feed drawing and upward-facing collision.
    for x in range(-1320, 1320, 440):
        for z in range(-1100, 1100, 440):
            color = (53, 71, 95, 255) if (x // 440 + z // 440) % 2 else (37, 49, 68, 255)
            walkable([(x, 0, z), (x, 0, z + 440), (x + 440, 0, z + 440), (x + 440, 0, z)], color)
    walkable([(720, 144, -180), (720, 0, 300), (1130, 0, 300), (1130, 144, -180)], (47, 144, 126, 255))
    walkable([(720, 144, -640), (720, 144, -180), (1130, 144, -180), (1130, 144, -640)], (87, 206, 165, 255))
    opaque.extend(panel(40, 610, 40, 510, -870, color=(242, 134, 61, 255)))
    opaque.extend(panel(310, 375, 40, 500, -620, color=(77, 62, 113, 255)))
    batches = [
        {"render_vertices": opaque},
        {"render_vertices": panel(-990, -105, 220, 590, -845, uv_max=(2048, 1024)), "texture": textures[0]},
        {"render_vertices": panel(-990, -105, 40, 175, -845, uv_max=(2048, 512)),
         "texture": textures[0], "state": 1},
        {"render_vertices": panel(40, 610, 40, 510, -700, uv_max=(1536, 1024)),
         "texture": textures[1], "pass": "translucent", "state": 2},
    ]
    return dict(version=2, id=scene_id, indexed_textures=[ci4, ci8],
                entrances=[dict(id=0, room=1, position=[0, 40, 720], yaw=-32768)],
                rooms=[dict(id=1, min=[-1320, -180, -1100], max=[1320, 850, 1100],
                            collision_vertices=vertices, collision_triangles=triangles, batches=batches,
                            states=[dict(scroll=[19, 0]), dict(tint=[87, 226, 255, 220])])])


def authored_wsc3_scene(scene_id):
    scene = authored_scene(scene_id)
    scene["version"] = 3
    room = scene["rooms"][0]
    room.pop("states")
    # Reuse the original walkable diagnostic floor and author the panels
    # independently of the WSC2 material batches.
    floor = room["batches"][0]["render_vertices"][:-12]
    grid = indexed("grid", 4, ("rgba5551", "10ff2f3f6fffbeff"),
                   lambda x, y: (x // 2 + y // 4) % 4)
    wave = indexed("wave", 8, ("ia88", "9040cc96ffe47000"),
                   lambda x, y: ((x - y) // 3) % 4)
    for image, name in zip((grid, wave), ("grid", "wave")):
        image["name"] = f"scenes/wsc3-{name}.rgba16"
    scene["indexed_textures"] = [grid, wave]
    tex0, tex1 = [{key: image[key] for key in ("name", "width", "height")} for image in (grid, wave)]
    dual = {
        "textures": [{**tex0, "address": ["repeat", "clamp"], "scroll": [17, 0]},
                     {**tex1, "address": ["mirror", "repeat"], "scale": [128, 384],
                      "origin": [96, -128], "scroll": [-13, 9], "filter": "three_point"}],
        "environment": [255, 245, 237, 24], "alpha_peak": 233, "alpha_period": 180,
    }
    floor.extend(panel(150, 1030, 100, 580, -730, color=(219, 115, 54, 255)))
    room["batches"] = [
        {"render_vertices": floor, "material": {}},
        {"render_vertices": panel(-1080, -95, 115, 590, -790, uv_max=(2304, 1280)), "material": dual},
        {"render_vertices": panel(205, 825, 125, 475, -690, uv_max=(1550, 1170)),
         "material": {**dual, "cutout": True}},
        {"render_vertices": panel(360, 1110, 240, 670, -590, uv_max=(1580, 1440)), "pass": "translucent",
         "material": {**dual, "primitive": [175, 255, 238, 158]}},
    ]
    return scene


def generate(output):
    scene_id, wsc3_id, invalid_id, prefix = build_config()
    scenes = [authored_scene(scene_id), authored_wsc3_scene(wsc3_id)]
    scene_dir = output / "scenes"
    scene_dir.mkdir(parents=True, exist_ok=True)
    bad_path = f"scenes/{invalid_id}.wsc"
    (output / bad_path).write_bytes(b"WSC2")  # intentional truncated input
    paths = [bad_path]
    for scene in scenes:
        good_path = f"scenes/{scene['id']}.wsc"
        (output / good_path).write_bytes(encode_scene(scene))
        paths.append(good_path)
        for name, (width, height, rgba) in indexed_textures(scene).items():
            path = f"gfx/{name.decode('ascii')}.png"
            write_rgba_png(output / path, width, height, rgba)
            paths.append(path)
    (output / "basepack.lst").write_text("".join(f"{output / path} {prefix}{path}\n" for path in paths))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    generate(parser.parse_args().output)
