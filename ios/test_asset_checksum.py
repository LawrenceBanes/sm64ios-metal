#!/usr/bin/env python3
"""Coverage for hash-only and sha256sum-format asset checksum files."""

from pathlib import Path
import tempfile
import unittest

from asset_checksum import parse_checksum


DIGEST = "8bf5230b6fa099a579334c6a55976b24377ae1f74b66e9361d50ac22c10a4083"


class AssetChecksumTests(unittest.TestCase):
    def parse_text(self, text: str) -> str:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "sm64-assets.sha256"
            path.write_text(text, encoding="utf-8")
            return parse_checksum(path, "sm64-assets.tar.gz")

    def test_hash_only(self) -> None:
        self.assertEqual(self.parse_text(DIGEST + "\n"), DIGEST)

    def test_sha256sum_with_absolute_archive_path(self) -> None:
        self.assertEqual(self.parse_text(DIGEST + "  /tmp/sm64-assets.tar.gz\n"), DIGEST)

    def test_sha256sum_binary_marker(self) -> None:
        self.assertEqual(self.parse_text(DIGEST + " *sm64-assets.tar.gz\n"), DIGEST)

    def test_rejects_wrong_archive_name(self) -> None:
        with self.assertRaisesRegex(ValueError, "must identify"):
            self.parse_text(DIGEST + "  other.tar.gz\n")

    def test_rejects_multiple_lines(self) -> None:
        with self.assertRaisesRegex(ValueError, "exactly one line"):
            self.parse_text(DIGEST + "\n" + DIGEST + "\n")

    def test_rejects_malformed_digest(self) -> None:
        with self.assertRaisesRegex(ValueError, "64-character"):
            self.parse_text("not-a-hash  sm64-assets.tar.gz\n")


if __name__ == "__main__":
    unittest.main()
