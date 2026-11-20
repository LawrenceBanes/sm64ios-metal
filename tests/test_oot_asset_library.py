"""Original authored resources validate deterministic library creation and updates."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from zipfile import ZipFile
from test_o2r_to_wsc3 import fixture, export_fixture
from tools.oot_asset_library import build, update

class LibraryTests(unittest.TestCase):
    def test_all_sources_preserved_runtime_resources_deterministic_and_incremental(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            exported = export_fixture(root)
            source = root / 'workshop.o2r'
            a, b = root / 'a.zip', root / 'b.zip'
            manifest = build(source, a, [exported])
            build(source, b, [exported])
            self.assertEqual(a.read_bytes(), b.read_bytes())
            with ZipFile(source) as original, ZipFile(a) as library:
                for name in original.namelist():
                    self.assertEqual(original.read(name), library.read('oot-assets/source/' + name))
                for record in manifest['runtime_files']:
                    data = library.read('oot-assets/' + record['path'])
                    self.assertEqual(hashlib.sha256(data).hexdigest(), record['sha256'])
                    self.assertEqual(len(data), record['bytes'])
            # Reusing an unchanged export sends only the new manifest/index.
            overlay = root / 'update.zip'
            update(a, overlay, [exported])
            with ZipFile(overlay) as archive:
                self.assertEqual(set(archive.namelist()), {'oot-assets/manifest.json', 'oot-assets/runtime-index.tsv'})
            self.assertTrue(any('runtime_conversion_blocker' in r for r in manifest['source_files']))

if __name__ == '__main__':
    unittest.main()
