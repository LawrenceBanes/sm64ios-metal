#!/usr/bin/env python3
"""Validate the structure and iPhoneOS arm64 executable in an unsigned IPA."""

import argparse
import plistlib
import struct
import subprocess
import zipfile
from pathlib import PurePosixPath


CPU_TYPE_ARM64 = 0x0100000C


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


def validate(ipa_path: str, require_system_deps: bool) -> None:
    with zipfile.ZipFile(ipa_path) as ipa:
        bad_member = ipa.testzip()
        if bad_member:
            raise ValueError(f"corrupt IPA member: {bad_member}")
        names = ipa.namelist()
        for name in names:
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts:
                raise ValueError(f"unsafe IPA member path: {name!r}")
        app_roots = sorted({p.split("/", 2)[1] for p in names if p.startswith("Payload/") and len(p.split("/", 2)) > 2 and p.split("/", 2)[1].endswith(".app")})
        if len(app_roots) != 1:
            raise ValueError(f"IPA must contain exactly one app bundle under Payload (found {app_roots})")
        app = "Payload/" + app_roots[0] + "/"
        info_path = app + "Info.plist"
        try:
            info = plistlib.loads(ipa.read(info_path))
        except KeyError as exc:
            raise ValueError("app bundle has no Info.plist") from exc
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
    print(f"executable: {executable_name}; Mach-O slices: {slices}")
    print("signature: unsigned structure check only; SideStore signing and device launch remain unverified")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipa")
    parser.add_argument("--require-system-deps", action="store_true", help="inspect linked dylibs with Apple's otool")
    args = parser.parse_args()
    validate(args.ipa, args.require_system_deps)


if __name__ == "__main__":
    main()
