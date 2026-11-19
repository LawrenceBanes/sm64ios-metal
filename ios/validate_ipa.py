#!/usr/bin/env python3
"""Validate the structure and iPhoneOS arm64 executable in an unsigned IPA."""

import argparse
import io
import plistlib
import struct
import subprocess
import zipfile
from pathlib import PurePosixPath


CPU_TYPE_ARM64 = 0x0100000C
SHITBOX_IDENTITY = {
    "CFBundleDisplayName": "SHITBOX LABZ INC.",
    "CFBundleName": "ShitboxDev",
    "CFBundleIdentifier": "com.larry.shitboxdev",
    "CFBundleShortVersionString": "0.69.420",
    "CFBundleVersion": "1",
    "ShitboxReleaseVersion": "beta.069420INITIAL",
}
BUILD_INTERMEDIATE_SUFFIXES = {
    ".a", ".bc", ".d", ".dia", ".dsym", ".gcda", ".gcno", ".hmap",
    ".o", ".pch", ".profdata", ".profraw", ".swiftmodule", ".swiftdoc",
    ".swiftsourceinfo", ".xcactivitylog", ".xcresult", ".xccheckout",
    ".xcuserstate", ".stringsdata", ".ds_store",
}
BUILD_INTERMEDIATE_COMPONENTS = {
    "__pycache__", "deriveddata", "intermediates.noindex", "index.noindex",
    "modulecache.noindex", "sourcepackages", "xcuserdata",
}
BUILD_INTERMEDIATE_NAMES = {"basepack.lst", ".ios_world_mode"}


def reject_build_intermediates(members: list[str], container: str) -> None:
    for member in members:
        path = PurePosixPath(member)
        if (
            path.suffix.lower() in BUILD_INTERMEDIATE_SUFFIXES
            or path.name.lower() in BUILD_INTERMEDIATE_NAMES
            or any(part.lower() in BUILD_INTERMEDIATE_COMPONENTS for part in path.parts)
        ):
            raise ValueError(f"{container} contains build intermediate: {member}")


def thin_macho_slice(data: bytes) -> tuple[int, int | None] | None:
    if len(data) < 8:
        return None
    magic = data[:4]
    if magic == b"\xcf\xfa\xed\xfe":
        endian = "<"
    elif magic == b"\xfe\xed\xfa\xcf":
        endian = ">"
    else:
        return None
    if len(data) < 32:
        return None
    cpu_type = struct.unpack_from(endian + "I", data, 4)[0]
    command_count = struct.unpack_from(endian + "I", data, 16)[0]
    command_bytes = struct.unpack_from(endian + "I", data, 20)[0]
    if command_count > 10000 or 32 + command_bytes > len(data):
        return None
    platform = None
    cursor = 32
    for _ in range(command_count):
        if cursor + 8 > len(data):
            return None
        command, command_size = struct.unpack_from(endian + "II", data, cursor)
        if command_size < 8 or cursor + command_size > len(data):
            return None
        if command == 0x32 and command_size >= 24:  # LC_BUILD_VERSION
            platform = struct.unpack_from(endian + "I", data, cursor + 8)[0]
        elif command == 0x25 and command_size >= 16:  # LC_VERSION_MIN_IPHONEOS
            platform = 2
        cursor += command_size
    return cpu_type, platform


def macho_slices(data: bytes) -> dict[int, int | None]:
    thin = thin_macho_slice(data)
    if thin is not None:
        return {thin[0]: thin[1]}
    magic = data[:4]
    if magic in (b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
        endian = ">" if magic == b"\xca\xfe\xba\xbe" else "<"
        count = struct.unpack_from(endian + "I", data, 4)[0]
        if count > 128 or 8 + count * 20 > len(data):
            return {}
        entries = []
        for i in range(count):
            entry = 8 + i * 20
            cpu_type, _, offset, size, _ = struct.unpack_from(endian + "IIIII", data, entry)
            entries.append((cpu_type, offset, size))
        return fat_slices(data, entries)
    if magic in (b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"):
        endian = ">" if magic == b"\xca\xfe\xba\xbf" else "<"
        count = struct.unpack_from(endian + "I", data, 4)[0]
        if count > 128 or 8 + count * 32 > len(data):
            return {}
        entries = []
        for i in range(count):
            entry = 8 + i * 32
            cpu_type, _, offset, size, _, _ = struct.unpack_from(endian + "IIQQII", data, entry)
            entries.append((cpu_type, offset, size))
        return fat_slices(data, entries)
    return {}


def fat_slices(data: bytes, entries: list[tuple[int, int, int]]) -> dict[int, int | None]:
    result = {}
    for cpu_type, offset, size in entries:
        if offset + size > len(data):
            return {}
        thin = thin_macho_slice(data[offset : offset + size])
        if thin is None or thin[0] != cpu_type:
            return {}
        result[thin[0]] = thin[1]
    return result


def check_scene_diagnostic(resource_pack: zipfile.ZipFile) -> None:
    prefix = "scene-diagnostic/"
    try:
        scene = resource_pack.read(prefix + "scenes/4098.wsc")
        invalid = resource_pack.read(prefix + "scenes/4294967294.wsc")
        wsc3 = resource_pack.read(prefix + "scenes/4099.wsc")
        textures = [resource_pack.read(prefix + f"gfx/scenes/67f-ci{bits}.rgba16.png") for bits in (4, 8)]
        textures.extend(resource_pack.read(prefix + f"gfx/scenes/wsc3-{name}.rgba16.png")
                        for name in ("grid", "wave"))
    except KeyError as exc:
        raise ValueError("WSC2/WSC3 diagnostic resource missing from base.zip") from exc
    if len(scene) <= 14 or scene[:8] != struct.pack("<4sI", b"WSC2", 4098) or invalid != b"WSC2":
        raise ValueError("incorrect WSC2 diagnostic package or failed-load probe")
    if len(wsc3) <= 14 or wsc3[:8] != struct.pack("<4sI", b"WSC3", 4099):
        raise ValueError("incorrect WSC3 diagnostic package")
    for png in textures:
        if not png.startswith(b"\x89PNG\r\n\x1a\n") or png[12:29] != struct.pack(">4sIIBBBBB", b"IHDR", 16, 16, 8, 6, 0, 0, 0):
            raise ValueError("WSC2/WSC3 diagnostic textures must be 16x16 RGBA PNGs")


LOCAL_SCENE_STRINGS = (b"Load Local WSC3 Scene", b"Leave Local WSC3 Scene", b"scenes/4100.wsc")
STOCK_AUDIO = ("sound/sequences.bin", "sound/sound_data.ctl", "sound/sound_data.tbl")
BUNDLED_SCENE_PACKAGES = {
    "scene-diagnostic/scenes/4098.wsc",
    "scene-diagnostic/scenes/4099.wsc",
    "scene-diagnostic/scenes/4294967294.wsc",
}


def reject_external_worlds(members, container, allowed_packages=()):
    for name in members:
        normalized = name.lower()
        if (PurePosixPath(normalized).suffix in {".o2r", ".otr", ".z64", ".n64", ".v64"}
                or (normalized.endswith(".wsc") and normalized not in allowed_packages)
                or normalized.startswith(("gfx/scenes/", "gfx/levels/hyrule_field/"))):
            raise ValueError(f"{container} contains a bundled external world asset: {name}")


def validate(ipa_path: str, require_system_deps: bool, require_game_textures: bool = False,
             require_scene_diagnostic: bool = False, require_shitbox_identity: bool = False,
             require_local_scene_loader: bool = False) -> None:
    with zipfile.ZipFile(ipa_path) as ipa:
        bad_member = ipa.testzip()
        if bad_member:
            raise ValueError(f"corrupt IPA member: {bad_member}")
        names = ipa.namelist()
        for name in names:
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts:
                raise ValueError(f"unsafe IPA member path: {name!r}")
        if "Payload/" not in names:
            raise ValueError("IPA is missing the Payload directory entry")
        app_roots = sorted({p.split("/", 2)[1] for p in names if p.startswith("Payload/") and len(p.split("/", 2)) > 2 and p.split("/", 2)[1].endswith(".app")})
        if len(app_roots) != 1:
            raise ValueError(f"IPA must contain exactly one app bundle under Payload (found {app_roots})")
        app = "Payload/" + app_roots[0] + "/"
        if app not in names:
            raise ValueError("IPA is missing the app bundle directory entry")
        app_members = [name[len(app):] for name in names if name.startswith(app)]
        reject_build_intermediates(app_members, "app bundle")
        if require_local_scene_loader:
            reject_external_worlds(app_members, "app bundle")
        app_file_count = sum(not member.endswith("/") for member in app_members)
        resource_pack_path = app + "res/base.zip"
        resource_pack_file_count = None
        if resource_pack_path in names:
            try:
                with zipfile.ZipFile(io.BytesIO(ipa.read(resource_pack_path))) as resource_pack:
                    bad_member = resource_pack.testzip()
                    if bad_member:
                        raise ValueError(f"app resource pack has corrupt member: {bad_member}")
                    resource_members = resource_pack.namelist()
                    reject_build_intermediates(resource_members, "app resource pack")
                    resource_pack_file_count = sum(not member.endswith("/") for member in resource_members)
                    if require_scene_diagnostic or require_local_scene_loader:
                        check_scene_diagnostic(resource_pack)
                    if require_local_scene_loader:
                        reject_external_worlds(resource_members, "app resource pack", BUNDLED_SCENE_PACKAGES)
                        audio = {item.filename for item in resource_pack.infolist() if item.file_size > 0}
                        missing = [name for name in STOCK_AUDIO if name not in audio]
                        if missing:
                            raise ValueError("missing or empty stock audio resources: " + ", ".join(missing))
            except zipfile.BadZipFile as exc:
                raise ValueError("app resource pack is corrupt") from exc
        elif require_scene_diagnostic or require_local_scene_loader:
            raise ValueError("WSC diagnostic IPA is missing res/base.zip")
        info_path = app + "Info.plist"
        try:
            info = plistlib.loads(ipa.read(info_path))
        except KeyError as exc:
            raise ValueError("app bundle has no Info.plist") from exc
        if require_shitbox_identity:
            for key, expected in SHITBOX_IDENTITY.items():
                if info.get(key) != expected:
                    raise ValueError(f"IPA Info.plist {key} must be {expected!r}, got {info.get(key)!r}")
        scene_manifest = info.get("UIApplicationSceneManifest")
        if not isinstance(scene_manifest, dict):
            raise ValueError("app Info.plist is missing its UIKit application scene configuration")
        scene_configurations = scene_manifest.get("UISceneConfigurations")
        app_scenes = scene_configurations.get("UIWindowSceneSessionRoleApplication", []) if isinstance(scene_configurations, dict) else []
        if not app_scenes:
            raise ValueError("app Info.plist is missing its UIKit application scene configuration")
        if not any(
            config.get("UISceneDelegateClassName") == "SM64SceneDelegate"
            for config in app_scenes
            if isinstance(config, dict)
        ):
            raise ValueError("app scene configuration does not name SM64SceneDelegate")
        executable_name = info.get("CFBundleExecutable")
        if not executable_name or "/" in executable_name:
            raise ValueError("Info.plist has an invalid CFBundleExecutable")
        executable_path = app + executable_name
        try:
            executable = ipa.read(executable_path)
        except KeyError as exc:
            raise ValueError(f"app executable is missing: {executable_name}") from exc
        slices = macho_slices(executable)
        if slices.get(CPU_TYPE_ARM64) != 2:  # PLATFORM_IOS; 7 is iOS Simulator
            raise ValueError(f"main executable lacks an arm64 iPhoneOS Mach-O slice (slices: {slices})")
        if b"SM64SceneDelegate" not in executable:
            raise ValueError("main executable does not contain the configured SM64SceneDelegate class")
        if require_local_scene_loader and not all(marker in executable for marker in LOCAL_SCENE_STRINGS):
            raise ValueError("main executable is missing the Documents WSC3 loader or its menu actions")
        if "iPhoneOS" not in info.get("CFBundleSupportedPlatforms", []):
            raise ValueError("app bundle is not marked for iPhoneOS")
        if not info.get("CFBundleIdentifier"):
            raise ValueError("Info.plist has no bundle identifier")
        if info.get("UIFileSharingEnabled") is not True:
            raise ValueError("app Info.plist must expose Documents through iOS Files")
        if info.get("LSSupportsOpeningDocumentsInPlace") is not True:
            raise ValueError("app Info.plist must allow Files to open app Documents in place")
        if 1 not in info.get("UIDeviceFamily", []):
            raise ValueError("app does not declare iPhone device support")
        orientations = set(info.get("UISupportedInterfaceOrientations", []))
        if not {"UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"}.issubset(orientations):
            raise ValueError("app metadata does not enable both landscape orientations")
        if app + "Assets.car" not in names:
            raise ValueError("app bundle is missing the compiled touch-control asset catalog")
        if require_game_textures:
            try:
                resource_pack_data = ipa.read(resource_pack_path)
            except KeyError as exc:
                raise ValueError("app bundle is missing res/base.zip with original game textures") from exc
            required_textures = (
                "gfx/actors/goomba/goomba_face.rgba16.png",
                "gfx/levels/bob/0.rgba16.png",
                "gfx/levels/castle_inside/1.rgba16.png",
            )
            try:
                with zipfile.ZipFile(io.BytesIO(resource_pack_data)) as resource_pack:
                    missing_textures = [name for name in required_textures if name not in resource_pack.namelist()]
                    if missing_textures:
                        raise ValueError("app original texture pack is missing: " + ", ".join(missing_textures))
                    for name in required_textures:
                        if not resource_pack.read(name).startswith(b"\x89PNG\r\n\x1a\n"):
                            raise ValueError(f"bundled original texture is not a PNG: {name}")
            except zipfile.BadZipFile as exc:
                raise ValueError("app bundle res/base.zip is corrupt") from exc
        for storyboard_key in ("UIMainStoryboardFile", "UILaunchStoryboardName"):
            storyboard = info.get(storyboard_key)
            if storyboard and not any(
                name.startswith(app + f"Base.lproj/{storyboard}.storyboardc/")
                for name in names
            ):
                raise ValueError(f"app bundle is missing compiled storyboard resources: {storyboard}")
        if app + "embedded.mobileprovision" in names:
            raise ValueError("expected an unsigned IPA, but an embedded provisioning profile was found")
        if app + "_CodeSignature/CodeResources" in names:
            raise ValueError("expected an unsigned IPA, but a code signature was found")

        frameworks = [n for n in names if n.startswith(app + "Frameworks/") and n.endswith(".framework/Info.plist")]
        for framework_info in frameworks:
            framework_root = framework_info.rsplit("/", 1)[0] + "/"
            framework = plistlib.loads(ipa.read(framework_info))
            binary_name = framework.get("CFBundleExecutable")
            framework_slices = macho_slices(ipa.read(framework_root + binary_name)) if binary_name else {}
            if framework_slices.get(CPU_TYPE_ARM64) != 2:
                raise ValueError(f"embedded framework is missing an arm64 iPhoneOS executable: {framework_root}")

    if require_system_deps:
        import tempfile

        with zipfile.ZipFile(ipa_path) as ipa, tempfile.NamedTemporaryFile(prefix="sm64-ipa-") as executable_file:
            executable_file.write(ipa.read(app + executable_name))
            executable_file.flush()
            output = subprocess.check_output(["xcrun", "otool", "-L", executable_file.name], text=True)
        dependencies = [line.strip().split(" ", 1)[0] for line in output.splitlines()[1:] if line.strip()]
        non_system = [p for p in dependencies if not p.startswith(("/System/Library/", "/usr/lib/", "@rpath/"))]
        if non_system:
            raise ValueError("unexpected non-system dynamic dependencies: " + ", ".join(non_system))
        missing_rpath = [
            p for p in dependencies
            if p.startswith("@rpath/") and app + "Frameworks/" + p[len("@rpath/"):] not in names
        ]
        if missing_rpath:
            raise ValueError("@rpath dependencies are not embedded in the app: " + ", ".join(missing_rpath))
    print(f"valid iPhoneOS arm64 IPA: {ipa_path}")
    print(f"bundle: {info['CFBundleIdentifier']} ({info.get('CFBundleShortVersionString', 'version unknown')})")
    if require_shitbox_identity:
        print(f"app release: {info['CFBundleDisplayName']} {info['ShitboxReleaseVersion']}")
    print(f"executable: {executable_name}; Mach-O slices: {slices}")
    print(f"app bundle file entries: {app_file_count}")
    if resource_pack_file_count is not None:
        print(f"runtime resource pack file entries: {resource_pack_file_count}")
    print("build intermediates: 0")
    if require_scene_diagnostic or require_local_scene_loader:
        print("WSC diagnostic resources: WSC2/WSC3 rooms, textures, and failed-load probe present")
    if require_local_scene_loader:
        print("Local WSC3 loader: menu, Documents package slot, and stock audio present; no external world bundled")
    print("signature: unsigned structure check only; SideStore signing and device launch remain unverified")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa")
    parser.add_argument("--require-system-deps", action="store_true", help="inspect linked dylibs with Apple's otool")
    parser.add_argument("--require-game-textures", action="store_true", help="require original Castle, Battlefield, and Goomba PNGs in res/base.zip")
    parser.add_argument("--require-scene-diagnostic", action="store_true", help="require the original WSC2/WSC3 diagnostic resources in res/base.zip")
    parser.add_argument("--require-shitbox-identity", action="store_true", help="verify the SHITBOX LABZ INC. name, bundle ID, and release versions")
    parser.add_argument("--require-local-scene-loader", action="store_true",
                        help="require the Documents WSC3 loader and stock audio, and reject bundled external worlds")
    args = parser.parse_args()
    validate(args.ipa, args.require_system_deps, args.require_game_textures,
             args.require_scene_diagnostic, args.require_shitbox_identity, args.require_local_scene_loader)


if __name__ == "__main__":
    main()
