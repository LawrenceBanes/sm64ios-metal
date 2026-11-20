#!/usr/bin/env python3
"""Bounded GitHub rate-limit and API-failure handling tests."""
from pathlib import Path
import os
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "ios/github_api.sh"


class GitHubApiHelperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.temp_path = Path(self.temp.name)
        self.gh = self.temp_path / "gh"
        self.log = self.temp_path / "gh-calls.log"
        self.gh.write_text(textwrap.dedent("""\
            #!/usr/bin/env python3
            import os, sys
            with open(os.environ["GH_FAKE_LOG"], "a") as calls:
                calls.write(" ".join(sys.argv[1:]) + "\\n")
            args = sys.argv[1:]
            if args[:2] == ["api", "rate_limit"]:
                print(os.environ.get("GH_FAKE_QUOTA", "20 2000000000"))
                raise SystemExit(0)
            mode = os.environ.get("GH_FAKE_MODE", "ok")
            if mode == "rate-limit":
                print("HTTP 403: API rate limit exceeded", file=sys.stderr)
                raise SystemExit(1)
            if mode == "forbidden":
                print("HTTP 403: Resource not accessible by integration", file=sys.stderr)
                raise SystemExit(1)
            print("ok")
        """))
        self.gh.chmod(0o755)
        self.env = os.environ.copy()
        self.env["PATH"] = f"{self.temp_path}:{self.env['PATH']}"
        self.env["GH_FAKE_LOG"] = str(self.log)
        self.env["GH_TEST_TOKEN"] = "fake-secret-token"

    def run_bash(self, code, *, mode="ok", quota="20 2000000000"):
        env = self.env.copy()
        env["GH_FAKE_MODE"] = mode
        env["GH_FAKE_QUOTA"] = quota
        return subprocess.run(["bash", "-c", f"source '{HELPER}'; {code}"],
                              env=env, text=True, capture_output=True)

    def test_preflight_reports_remaining_budget_and_reset(self):
        result = self.run_bash('github_api_preflight "$GH_TEST_TOKEN" 12')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("remaining=20 minimum=12", result.stdout)
        self.assertIn("reset=2033-05-18", result.stdout)

    def test_low_budget_fails_before_any_asset_api_request(self):
        result = self.run_bash(
            'github_api_preflight "$GH_TEST_TOKEN" 12 && github_api_call assets "$GH_TEST_TOKEN" gh release view tag',
            quota="3 2000000000",
        )
        self.assertEqual(result.returncode, 75)
        self.assertIn("stopping before private asset download", result.stderr)
        self.assertEqual(self.log.read_text().splitlines(), ["api rate_limit --jq .resources.core | [.remaining, .reset] | @tsv"])

    def test_rate_limit_is_classified_and_never_retried(self):
        result = self.run_bash(
            'github_api_preflight "$GH_TEST_TOKEN" 12 && github_api_call assets "$GH_TEST_TOKEN" gh release view tag',
            mode="rate-limit",
        )
        self.assertEqual(result.returncode, 75)
        self.assertIn("No automatic retry was attempted", result.stderr)
        self.assertNotIn("fake-secret-token", result.stdout + result.stderr)
        self.assertEqual(len(self.log.read_text().splitlines()), 2)

    def test_permission_403_is_not_misreported_as_rate_limit(self):
        result = self.run_bash(
            'github_api_preflight "$GH_TEST_TOKEN" 12 && github_api_call assets "$GH_TEST_TOKEN" gh release view tag',
            mode="forbidden",
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("not identified as a rate-limit response", result.stderr)
        self.assertNotIn("fake-secret-token", result.stdout + result.stderr)
        self.assertEqual(len(self.log.read_text().splitlines()), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
