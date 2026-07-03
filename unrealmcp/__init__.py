"""
UnrealMCP — MCP Server for Unreal Engine 5

Pip-installable package. After `pip install unrealmcp`, run the server with:
    unrealmcp              # stdio transport (default)

Or use directly in MCP configs:
    "command": "unrealmcp"
"""

try:
    from importlib.metadata import version as _pkg_version
    __version__ = _pkg_version("unrealmcp")
except Exception:  # not installed (running from a checkout)
    __version__ = "0.0.0.dev0"


def main():
    """Entry point for the `unrealmcp` console command."""
    from unrealmcp._bridge import mcp  # noqa: F401

    # Importing tools registers them via @mcp.tool() decorators
    import unrealmcp.tools  # noqa: F401

    mcp.run()
