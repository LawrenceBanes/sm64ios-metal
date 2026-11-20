#!/usr/bin/env python3
"""Build a private, deterministic complete O2R source library plus runtime resources."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import struct
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo
from . import o2r_inventory as inventory, hyrule_importer as o2r


def digest(data):
    return hashlib.sha256(data).hexdigest()


def build(archive, output, scene_exports=()):
    catalog = inventory.inventory_archive(archive, verify_crc=True)
    records, runtime, payloads = [], {}, {}
    source_sha = digest(archive.read_bytes())
    with ZipFile(archive) as source:
        for name, entry in sorted(inventory.archive_index(source).items()):
            data = source.read(entry)
            target = 'source/' + name
            payloads[target] = data
            record = catalog['resources'][name]
            records.append({'id': 'o2r:' + name, 'source': name, 'path': target,
                            'bytes': len(data), 'sha256': digest(data), 'type': record['type'],
                            'version': record['version'], 'conversion_status': record['status'],
                            'dependencies': record.get('dependencies', []),
                            'limitations': record.get('limitations', []), 'reason': record.get('reason')})
    # Whole-image, non-indexed textures use the exact content identity emitted
    # by the scene converter. Palette-dependent variants remain source-only.
    with tempfile.TemporaryDirectory() as temporary:
        png_path = Path(temporary) / 'texture.png'
        for record in records:
            if record['type'] != 'Texture':
                continue
            data = payloads[record['path']]
            header = o2r.read_resource_header(data[:o2r.OTR_HEADER_SIZE], record['source'])
            try:
                desc = o2r.parse_texture_descriptor(data[o2r.OTR_HEADER_SIZE:], '<' if header['endian'] == 'little' else '>', header['version'], record['source'])
                if desc['flags'] or desc['horizontal_byte_scale'] != 1 or desc['vertical_pixel_scale'] != 1:
                    raise ValueError('non-packed texture flags or scale')
                if not (0 < desc['width'] <= 1024 and 0 < desc['height'] <= 1024):
                    raise ValueError('runtime dimensions exceed 1024x1024')
                pixels = o2r.decode_texture_rgba(desc, data[o2r.OTR_HEADER_SIZE + desc['payload_offset']:])
                width, height = desc['width'], desc['height']
                key = digest(struct.pack('<HH', width, height) + pixels)
                relative = f'gfx/scenes/o2r-{key}.rgba16.png'
                o2r.write_rgba_png(png_path, width, height, pixels)
                encoded = png_path.read_bytes()
                if len(encoded) > 256 * 1024:
                    raise ValueError('runtime PNG exceeds 256 KiB limit')
                payloads[relative] = encoded
                runtime[relative] = {'id': relative, 'path': relative, 'bytes': len(encoded),
                                     'sha256': digest(encoded), 'width': width, 'height': height}
                record['runtime_id'] = relative
            except (ValueError, o2r.ImportFailure, KeyError) as error:
                record['runtime_conversion_blocker'] = str(error)
    for directory in scene_exports:
        report_path = directory / 'conversion-report.json'
        if report_path.exists():
            report = report_path.read_bytes()
            payloads['reports/' + digest(report) + '.json'] = report
        for path in sorted(directory.rglob('*')):
            if not path.is_file() or path.suffix not in ('.wsc', '.png'):
                continue
            relative = path.relative_to(directory).as_posix()
            o2r.safe_archive_path(relative)
            if not relative.startswith(('gfx/scenes/', 'scenes/')):
                raise ValueError('unexpected runtime namespace: ' + relative)
            data = path.read_bytes()
            if relative in payloads and payloads[relative] != data:
                raise ValueError('conflicting runtime asset: ' + relative)
            payloads[relative] = data
            runtime[relative] = {'id': relative, 'path': relative, 'bytes': len(data), 'sha256': digest(data)}
    index = 'OOTLIB1\n' + ''.join(f'{key}\t{value["path"]}\t{value["bytes"]}\n' for key, value in sorted(runtime.items()))
    payloads['runtime-index.tsv'] = index.encode()
    payloads['inventory.json'] = (json.dumps(catalog, sort_keys=True, indent=2) + '\n').encode()
    manifest = {'format': 'oot-complete-asset-library', 'version': 1,
                'source_archive_sha256': source_sha, 'revision': 'EC7011B7' if any(r['source'] == 'version' and payloads[r['path']] == bytes.fromhex('01ec7011b7') for r in records) else 'see source/version',
                'source_files': records, 'runtime_files': list(runtime.values()),
                'runtime_index_sha256': digest(payloads['runtime-index.tsv']),
                'coverage': catalog['totals'],
                'limitations': ['Parser status does not establish runtime convertibility.',
                    'All source entries retained; only supplied validated scene exports are runtime-ready.',
                    'Future materials may require new PNG variants and an incremental runtime update.',
                    'Source actor scripts and Zelda gameplay are not executed by SM64.']}
    payloads['manifest.json'] = (json.dumps(manifest, sort_keys=True, indent=2) + '\n').encode()
    if output.exists():
        raise ValueError('output already exists')
    with ZipFile(output, 'w', compression=ZIP_DEFLATED, compresslevel=9) as out:
        for name, data in sorted(payloads.items()):
            info = ZipInfo('oot-assets/' + name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            out.writestr(info, data)
    with ZipFile(output) as verify:
        if verify.testzip() is not None:
            raise ValueError('ZIP integrity failure')
        for name, data in payloads.items():
            if digest(verify.read('oot-assets/' + name)) != digest(data):
                raise ValueError('archive readback mismatch: ' + name)
    checksum = digest(output.read_bytes())
    output.with_suffix(output.suffix + '.sha256').write_text(f'{checksum}  {output.name}\n')
    return manifest


def update(base, output, scene_exports):
    """An overlay contains only changed runtime files plus the complete new index.

    Extract it over the installed library while outside any external scene.
    Original source entries remain untouched.
    """
    if output.exists():
        raise ValueError('output already exists')
    with ZipFile(base) as archive:
        manifest = json.loads(archive.read('oot-assets/manifest.json'))
    if manifest.get('format') != 'oot-complete-asset-library' or manifest.get('version') != 1:
        raise ValueError('incompatible base library')
    runtime = {entry['id']: entry for entry in manifest['runtime_files']}
    payloads = {}
    for directory in scene_exports:
        for path in sorted(directory.rglob('*')):
            if not path.is_file() or path.suffix not in ('.wsc', '.png'):
                continue
            relative = path.relative_to(directory).as_posix()
            o2r.safe_archive_path(relative)
            if not relative.startswith(('gfx/scenes/', 'scenes/')):
                raise ValueError('unexpected runtime namespace')
            data = path.read_bytes()
            record = {'id': relative, 'path': relative, 'bytes': len(data), 'sha256': digest(data)}
            if runtime.get(relative, {}).get('sha256') != record['sha256']:
                payloads[relative] = data
            runtime[relative] = record
    index = 'OOTLIB1\n' + ''.join(f'{key}\t{value["path"]}\t{value["bytes"]}\n' for key, value in sorted(runtime.items()))
    manifest['runtime_files'] = list(runtime.values())
    manifest['runtime_index_sha256'] = digest(index.encode())
    payloads['runtime-index.tsv'] = index.encode()
    payloads['manifest.json'] = (json.dumps(manifest, sort_keys=True, indent=2) + '\n').encode()
    with ZipFile(output, 'w', compression=ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(payloads.items()):
            info = ZipInfo('oot-assets/' + name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    with ZipFile(output) as archive:
        assert archive.testzip() is None
    output.with_suffix(output.suffix + '.sha256').write_text(f'{digest(output.read_bytes())}  {output.name}\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--scene-export', action='append', type=Path, default=[])
    parser.add_argument('--update-from', type=Path, help='create an incremental runtime overlay against an existing master ZIP')
    args = parser.parse_args()
    manifest = update(args.update_from, args.output, args.scene_export) if args.update_from else build(args.archive, args.output, args.scene_export)
    print(json.dumps({'source_files': len(manifest['source_files']), 'runtime_files': len(manifest['runtime_files']),
                      'coverage': manifest['coverage'], 'archive_bytes': args.output.stat().st_size}, indent=2))


if __name__ == '__main__':
    main()
