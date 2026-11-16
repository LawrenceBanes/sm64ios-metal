#!/usr/bin/env python3
"""Static contract checks for the manual-only Mission 55.5 Apple build."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
workflow = (ROOT / ".github/workflows/mission-55-5-connected-world.yml").read_text()

assert re.search(r"(?m)^on:\s*\n\s+workflow_dispatch:\s*$", workflow)
trigger_header = workflow.split("permissions:", 1)[0]
assert not re.search(r"(?m)^\s+(push|pull_request|schedule):", trigger_header)
assert "runs-on: xcode-27" in workflow
assert "timeout-minutes: 60" in workflow
assert "github.event.repository.private == true" in workflow
assert "contents: read" in workflow
assert "IOS_HYRULE_WORLD=0" in workflow
assert "IOS_CONNECTED_WORLD_TEST=1" in workflow
assert "ios/validate-metal-apple.sh" in workflow
assert "CODE_SIGNING_ALLOWED=NO" in workflow
assert "CODE_SIGNING_REQUIRED=NO" in workflow
assert "SM64_SKIP_CODESIGN=1" in workflow
assert "actions/upload-artifact@v4" in workflow
assert "retention-days: 1" in workflow
assert "gh release create" not in workflow
assert "--retry" not in workflow
print("Mission 55.5 workflow is manual-only, unsigned, connected-world configured, and artifact-only")
