"""Tool-surface guards: no silent count drift, no dropped duplicates, essentials present."""
import os
import re

from unrealmcp._bridge import mcp
import unrealmcp.tools  # noqa: F401 — registers every @mcp.tool()

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMMANDS_MD = os.path.join(HERE, "COMMANDS.md")


def _manifest_count() -> int:
    with open(COMMANDS_MD) as f:
        m = re.search(r"\*\*(\d+) tools\*\*", f.read())
    assert m, "COMMANDS.md missing '**N tools**' header"
    return int(m.group(1))


def test_live_count_matches_manifest():
    live = len(mcp._tool_manager._tools)
    manifest = _manifest_count()
    assert live == manifest, (
        f"Tool count drift: {live} live vs {manifest} in COMMANDS.md — "
        "run scripts/gen_command_manifest.py and update the README count."
    )


def test_manifest_lists_every_tool():
    with open(COMMANDS_MD) as f:
        body = f.read()
    for name in mcp._tool_manager._tools:
        assert f"`{name}`" in body, f"{name} missing from COMMANDS.md"


def test_essential_tools_present():
    for name in ("execute_python", "health_check", "wait_until_ready",
                 "take_screenshot", "contact_sheet"):
        assert name in mcp._tool_manager._tools, f"essential tool {name} not registered"
