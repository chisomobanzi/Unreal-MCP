"""
Tool registration — importing each module registers its @mcp.tool() decorators.

By default every module loads (the full ~300-tool surface). Set the env var
UNREAL_MCP_TOOLS to a comma-separated list of module names to load ONLY those
(plus `core`, which is always loaded) — this shrinks the schema the MCP server
advertises, useful for focused sessions on a harness without lazy tool loading.

    UNREAL_MCP_TOOLS=materials,level,editor_commands   # look-dev session
    UNREAL_MCP_TOOLS=niagara                            # VFX session

Module names: assets, materials, blueprints, level, data_tables,
blueprint_graph, editor_commands, data_assets, widgets, enhanced_input,
profiling, debug, niagara, statetree.
"""
import importlib
import logging
import os

logger = logging.getLogger("UnrealMCP.Tools")

# Optional tool modules (core is always loaded — execute_python / health_check
# / wait_until_ready are essential and must never be filtered out).
_ALL_MODULES = [
    "assets",
    "materials",
    "blueprints",
    "level",
    "data_tables",
    "blueprint_graph",
    "editor_commands",
    "data_assets",
    "widgets",
    "enhanced_input",
    "profiling",
    "debug",
    "niagara",
    "statetree",
]

from unrealmcp.tools import core  # noqa: F401 — mandatory

_selected = os.environ.get("UNREAL_MCP_TOOLS", "").strip()
if _selected:
    _wanted = {m.strip() for m in _selected.split(",") if m.strip()}
    _unknown = _wanted - set(_ALL_MODULES)
    if _unknown:
        logger.warning("UNREAL_MCP_TOOLS: unknown module(s) ignored: %s", ", ".join(sorted(_unknown)))
    _to_load = [m for m in _ALL_MODULES if m in _wanted]
    logger.info("UNREAL_MCP_TOOLS set — loading %d/%d modules: %s",
                len(_to_load), len(_ALL_MODULES), ", ".join(_to_load))
else:
    _to_load = _ALL_MODULES

for _mod in _to_load:
    importlib.import_module(f"unrealmcp.tools.{_mod}")
