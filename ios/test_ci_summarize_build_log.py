import unittest

from ci_summarize_build_log import summarize


class BuildSummaryTests(unittest.TestCase):
    def test_excludes_source_text_paths_and_secrets(self):
        log = """/private/build/secret-assets/actor.inc.c:123:4: error: use of undeclared identifier 'MarioTextureSecret'
123 | private character asset bytes
ld: error: private token ghp_please_do_not_print
"""
        output = "\n".join(summarize(log))
        self.assertIn("actor.inc.c:123: undeclared symbol", output)
        self.assertIn("arm64 linker: error", output)
        for secret in ("secret-assets", "MarioTextureSecret", "private character", "ghp_"):
            self.assertNotIn(secret, output)

    def test_reports_unclassified_failure_without_echoing_log(self):
        self.assertIn("No compiler diagnostic", "\n".join(summarize("private failure content")))

    def test_reports_only_sanitized_linker_symbol_names(self):
        log = '''Undefined symbols for architecture arm64:
  "_mod_runtime_before_phys_step", referenced from:
      _perform_ground_step in /private/build/secret/obj/mario_step.o
ld: symbol(s) not found for architecture arm64
'''
        output = "\n".join(summarize(log))
        self.assertIn("arm64 linker: undefined symbols", output)
        self.assertIn("arm64 linker missing: _mod_runtime_before_phys_step", output)
        self.assertNotIn("/private/build/secret", output)


if __name__ == "__main__":
    unittest.main()
