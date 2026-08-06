"""
TCP bridge for the C++ UnrealMCPBridge.

The C++ bridge handles Blueprint graph manipulation, editor commands, and
operations requiring direct access to K2Node APIs that aren't available
through the Python scripting plugin.

Port is auto-discovered from the port file written by the C++ bridge
at Saved/UnrealMCP/port.txt. This allows multiple Unreal Editor instances
to run simultaneously on different ports without configuration.

All tool modules that need the C++ bridge import `_tcp_send` from here.
"""

import glob
import json
import os
import socket
import logging
import struct
import time
import threading
from typing import Any, Dict, Optional

logger = logging.getLogger("UnrealMCP.TCPBridge")

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
TCP_HOST = "127.0.0.1"
DEFAULT_PORT = 55557
PORT_FILE = os.path.join("Saved", "UnrealMCP", "port.txt")
CONNECT_TIMEOUT = 10  # seconds
# Must EXCEED the C++ bridge's game-thread deadline (120s): the server always
# answers by then (with its own timeout error if the command is still running),
# so the client should wait it out rather than race it and retry a command
# that is still executing.
DEFAULT_RECV_TIMEOUT = 130  # seconds
LARGE_OP_RECV_TIMEOUT = 300  # seconds
BUFFER_SIZE = 65536  # 64 KB recv chunk size
MAX_RETRIES = 3
BASE_RETRY_DELAY = 0.5  # seconds

LARGE_OPERATION_COMMANDS = {
    "get_available_materials",
    "read_blueprint_content",
    "analyze_blueprint_graph",
    "build_material_graph",
    "build_material_function_graph",
    "performance_start_trace",
    "performance_analyze_insight",
    "add_widget",
    "set_widget_properties",
    "duplicate_widget",
    "execute_python",
    "save_all",
    "save_asset",
    "import_asset",
    "import_assets_batch",
    "compile_blueprint",
    "compile_niagara_system",
    "compile_statetree",
    "take_screenshot",
    "screenshot_widget",
    "capture_from_camera",
    "contact_sheet",
}


class PostSendError(Exception):
    """A failure that occurred AFTER the request was fully sent.

    The editor may still be executing the command, so it must NOT be
    retried — retrying would risk running a mutating command twice.
    """


def _find_project_root() -> Optional[str]:
    """Locate the UE project root (the directory containing a .uproject).

    Priority:
        1. UNREAL_MCP_PROJECT_DIR environment variable
        2. Walk up from the current working directory
    """
    env_dir = os.environ.get("UNREAL_MCP_PROJECT_DIR")
    if env_dir and os.path.isdir(env_dir):
        return env_dir

    d = os.getcwd()
    while True:
        if glob.glob(os.path.join(d, "*.uproject")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def _port_file_path() -> str:
    """Absolute port-file path anchored to the project root (not the CWD)."""
    root = _find_project_root()
    if root:
        return os.path.join(root, PORT_FILE)
    return PORT_FILE  # legacy CWD-relative fallback


def _resolve_port() -> int:
    """Resolve the TCP port for the C++ bridge.

    Priority:
        1. UNREAL_MCP_PORT environment variable (manual override)
        2. <project root>/Saved/UnrealMCP/port.txt (auto-written by the editor)
        3. Default 55557
    """
    env_port = os.environ.get("UNREAL_MCP_PORT")
    if env_port:
        try:
            port = int(env_port)
            if 1 <= port <= 65535:
                logger.info("Using port %d from UNREAL_MCP_PORT env var", port)
                return port
        except ValueError:
            logger.warning("Invalid UNREAL_MCP_PORT value: %s", env_port)

    port_file = _port_file_path()
    try:
        if os.path.exists(port_file):
            with open(port_file, "r", encoding="utf-8") as f:
                port = int(f.read().strip())
                if 1 <= port <= 65535:
                    logger.info("Using port %d from %s", port, port_file)
                    return port
    except (ValueError, OSError) as exc:
        logger.warning("Failed to read port file: %s", exc)

    logger.info("Using default port %d", DEFAULT_PORT)
    return DEFAULT_PORT


def _latest_crash_dir(root: str) -> Optional[str]:
    """Newest crashinfo-* directory under Saved/Crashes, if any."""
    crashes = os.path.join(root, "Saved", "Crashes")
    try:
        dirs = [
            os.path.join(crashes, d)
            for d in os.listdir(crashes)
            if d.startswith("crashinfo-")
        ]
    except OSError:
        return None
    if not dirs:
        return None
    return max(dirs, key=os.path.getmtime)


def _crash_summary(crash_dir: str) -> str:
    """First assertion/fatal line from a crash dir's log, if readable."""
    markers = ("Assertion failed", "Fatal error", "SIGSEGV", "SIGILL", "SIGABRT")
    try:
        for name in os.listdir(crash_dir):
            if not name.endswith(".log"):
                continue
            with open(
                os.path.join(crash_dir, name),
                "r", encoding="utf-8", errors="replace",
            ) as f:
                for line in f:
                    if any(m in line for m in markers):
                        return line.strip()[:300]
    except OSError:
        pass
    return "(crash log unreadable)"


def _diagnose_connection_failure(port: int) -> str:
    """Turn a bare 'cannot connect' into an actionable message."""
    base = f"Cannot connect to C++ bridge at {TCP_HOST}:{port}."
    root = _find_project_root()
    if root is None:
        return (
            f"{base} No .uproject found walking up from the CWD, so the "
            "editor's port file cannot be located — start the MCP client "
            "from inside the UE project, or set UNREAL_MCP_PROJECT_DIR."
        )

    port_file = os.path.join(root, PORT_FILE)
    if not os.path.exists(port_file):
        return (
            f"{base} The editor is not running (no port file at "
            f"{port_file}). Launch it (e.g. ./launch-editor.sh) and wait "
            "for it to finish loading."
        )

    crash_dir = _latest_crash_dir(root)
    try:
        port_mtime = os.path.getmtime(port_file)
    except OSError:
        port_mtime = 0.0
    if crash_dir and os.path.getmtime(crash_dir) >= port_mtime:
        return (
            f"{base} The editor appears to have CRASHED: "
            f"{_crash_summary(crash_dir)} (see {crash_dir}). "
            "Relaunch with ./launch-editor.sh."
        )

    return (
        f"{base} A port file exists ({port_file}) but nothing is "
        "listening — the editor may still be starting up or shutting "
        "down, or the file is stale. If the editor is running, check "
        "the UnrealMCP plugin output; otherwise relaunch it."
    )


# ---------------------------------------------------------------------------
# TCP Connection
# ---------------------------------------------------------------------------
class _TCPConnection:
    """Thread-safe TCP connection to the C++ MCP bridge.

    Uses length-prefix framing: each response is preceded by a 4-byte
    big-endian length header, followed by that many bytes of JSON payload.

    The port is resolved lazily on first connection and refreshed
    on connection failure (handles editor restarts on a different port).
    """

    def __init__(self):
        self._sock: Optional[socket.socket] = None
        self._lock = threading.RLock()
        self._port: Optional[int] = None

    def _get_port(self, force_refresh: bool = False) -> int:
        if self._port is None or force_refresh:
            self._port = _resolve_port()
        return self._port

    # -- low-level helpers --------------------------------------------------

    def _make_socket(self) -> socket.socket:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(CONNECT_TIMEOUT)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4 * 1024 * 1024)
        return sock

    def _close_unsafe(self):
        if self._sock:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except Exception:
                pass
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    def _ensure_connected(self) -> bool:
        """Connect only if not already connected. Reuses existing socket.

        If the resolved port (usually from port.txt) refuses the connection,
        probes the default port once — handles a stale port file left behind
        by a crashed/previous editor instance.
        """
        if self._sock is not None:
            # A parked socket may be a corpse: the single-client bridge closes the
            # previous connection whenever ANY other client connects (a 3s health
            # poller guarantees it), and sendall() into that corpse "succeeds" into
            # the kernel buffer — the command is never read, the response read gets
            # 0 bytes, and the no-retry-after-send rule turns it into a hard error
            # ("delivered to the editor but no response", 2026-08-07: every other
            # command failing for an entire evening). A zero-byte MSG_PEEK here
            # proves the far end closed BEFORE reading anything we are about to
            # send, so reconnecting is provably double-execution-safe.
            try:
                self._sock.setblocking(False)
                try:
                    peeked = self._sock.recv(1, socket.MSG_PEEK)
                    dead = (peeked == b"")   # orderly FIN already received
                except (BlockingIOError, InterruptedError):
                    dead = False             # nothing to read — socket healthy
                except OSError:
                    dead = True              # RST or worse
                finally:
                    self._sock.setblocking(True)
            except OSError:
                dead = True
            if not dead:
                return True
            logger.info("Parked bridge socket was closed by the editor — reconnecting")
            self._close_unsafe()
        port = self._get_port()
        for candidate in (port, DEFAULT_PORT):
            try:
                self._sock = self._make_socket()
                self._sock.connect((TCP_HOST, candidate))
                if candidate != port:
                    logger.warning(
                        "Port %d (from port file) refused; connected on "
                        "default %d — port file was stale", port, candidate,
                    )
                    self._port = candidate
                logger.info(
                    "Connected to C++ bridge at %s:%d", TCP_HOST, candidate,
                )
                return True
            except Exception:
                self._close_unsafe()
                if candidate == DEFAULT_PORT:
                    break
        return False

    def _recv_exact(self, n: int) -> bytes:
        """Read exactly n bytes from the socket."""
        data = bytearray()
        while len(data) < n:
            remaining = n - len(data)
            chunk = self._sock.recv(min(remaining, BUFFER_SIZE))
            if not chunk:
                raise ConnectionError(
                    f"Connection closed after {len(data)}/{n} bytes"
                )
            data.extend(chunk)
        return bytes(data)

    def _recv_response(self, command: str) -> bytes:
        """Receive a length-prefixed response from the C++ bridge."""
        if command in LARGE_OPERATION_COMMANDS:
            timeout = LARGE_OP_RECV_TIMEOUT
        else:
            timeout = DEFAULT_RECV_TIMEOUT
        self._sock.settimeout(timeout)

        # Read 4-byte big-endian length header
        header = self._recv_exact(4)
        payload_len = struct.unpack(">I", header)[0]

        if payload_len == 0:
            raise ConnectionError("Server sent empty response (length=0)")
        if payload_len > 100 * 1024 * 1024:
            raise ConnectionError(
                f"Response too large: {payload_len} bytes (>100MB)"
            )

        logger.info(
            "Expecting %d bytes for %s (timeout=%ds)",
            payload_len, command, timeout,
        )

        # Read exactly payload_len bytes of JSON
        return self._recv_exact(payload_len)

    # -- public API ---------------------------------------------------------

    def send_command(
        self,
        command: str,
        params: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Send a command to the C++ bridge and return the parsed response.

        Retries apply ONLY to failures that happen before the request was
        fully sent (i.e. connection failures). Once the command has been
        delivered, a lost/late response is NOT retried — the editor may
        still be executing it, and a blind retry would double-execute
        mutating commands (spawn/import/save/...).
        """
        last_error = ""
        for attempt in range(MAX_RETRIES + 1):
            try:
                return self._send_once(command, params)
            except PostSendError as exc:
                logger.error("TCP command %s: %s", command, exc)
                return {"status": "error", "error": str(exc)}
            except (ConnectionError, TimeoutError, socket.error, OSError) as exc:
                last_error = str(exc)
                logger.warning(
                    "TCP command %s failed (attempt %d): %s",
                    command, attempt + 1, exc,
                )
                with self._lock:
                    self._close_unsafe()
                    self._port = None
                if attempt < MAX_RETRIES:
                    delay = min(BASE_RETRY_DELAY * (2 ** attempt), 5.0)
                    time.sleep(delay)
            except Exception as exc:
                logger.error("Unexpected error sending TCP command: %s", exc)
                with self._lock:
                    self._close_unsafe()
                return {"status": "error", "error": str(exc)}

        return {
            "status": "error",
            "error": (
                f"Failed after {MAX_RETRIES + 1} attempts: "
                f"{_diagnose_connection_failure(self._get_port())} "
                f"(last error: {last_error})"
            ),
        }

    def _send_once(
        self,
        command: str,
        params: Optional[Dict[str, Any]],
    ) -> Dict[str, Any]:
        with self._lock:
            if not self._ensure_connected():
                raise ConnectionError(
                    f"Cannot connect to C++ bridge at "
                    f"{TCP_HOST}:{self._get_port()}"
                )
            sent = False
            try:
                payload = json.dumps({
                    "type": command,
                    "params": params or {},
                })
                payload_bytes = payload.encode("utf-8")
                logger.info(
                    "Sending command: %s (%d bytes)",
                    command, len(payload_bytes),
                )
                # Length-prefix framing: 4-byte big-endian header + JSON
                # (matches the response protocol)
                length_header = struct.pack(">I", len(payload_bytes))
                self._sock.settimeout(10)
                self._sock.sendall(length_header + payload_bytes)
                sent = True
                logger.info("Send complete, waiting for response...")

                raw = self._recv_response(command)
                logger.info("Received response: %d bytes", len(raw))
                text = raw.decode("utf-8")
                try:
                    response = json.loads(text)
                except json.JSONDecodeError as e:
                    start = max(0, e.pos - 100)
                    end = min(len(text), e.pos + 50)
                    context = (
                        f"...{text[start:e.pos]}"
                        f">>>HERE>>>"
                        f"{text[e.pos:end]}..."
                    )
                    raise ValueError(
                        f"JSON parse error at char {e.pos}: {e.msg} | "
                        f"Context: {context}"
                    ) from e

                if response.get("success") is False and "status" not in response:
                    response["status"] = "error"

                return response
            except Exception as exc:
                logger.error("_send_once failed for %s: %s", command, exc)
                self._close_unsafe()
                if sent and not isinstance(exc, PostSendError):
                    raise PostSendError(
                        f"'{command}' was delivered to the editor but no "
                        f"response arrived ({exc}). The editor may still be "
                        "executing it — NOT retrying, to avoid double "
                        "execution. Verify the editor state before "
                        "re-issuing this command."
                    ) from exc
                raise


# ---------------------------------------------------------------------------
# Module-level singleton
# ---------------------------------------------------------------------------
_conn: Optional[_TCPConnection] = None
_conn_lock = threading.Lock()


def _get_conn() -> _TCPConnection:
    global _conn
    with _conn_lock:
        if _conn is None:
            _conn = _TCPConnection()
        return _conn


def _tcp_send(
    command: str,
    params: Optional[Dict[str, Any]] = None,
) -> str:
    """Send a command to the C++ MCP bridge and return a JSON string result.

    This is the TCP equivalent of ``_send()`` in ``_bridge.py``.
    Returns a JSON-formatted string for consistency with the HTTP bridge.
    """
    try:
        resp = _get_conn().send_command(command, params)
        return json.dumps(resp, default=str, indent=2)
    except Exception as exc:
        return json.dumps({"status": "error", "error": str(exc)})


def _tcp_send_raw(
    command: str,
    params: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """Send a command and return the raw dict (not JSON-serialized)."""
    try:
        return _get_conn().send_command(command, params)
    except Exception as exc:
        return {"status": "error", "error": str(exc)}


def _format_debug_header(debug: dict) -> str:
    """Format a visible token debug header from the _debug field."""
    cmd = debug.get("command", "?")
    tokens = debug.get("estimated_tokens", 0)
    chars = debug.get("response_chars", 0)
    calls = debug.get("call_count", 0)
    avg = debug.get("avg_tokens", 0)
    max_t = debug.get("max_tokens", 0)

    line = f"[TOKEN DEBUG] {cmd} — ~{tokens:,} tokens ({chars:,} chars)"
    if calls > 1:
        line += f" | avg ~{avg:,}, max ~{max_t:,}, calls {calls}"

    separator = "─" * min(len(line), 70)
    return f"{line}\n{separator}\n"


def _call(command: str, params: dict | None = None) -> str:
    """Send a command and return the JSON-formatted response string.

    When token debug is enabled, prepends a visible header with token
    estimates so it's always visible at the top, even for long responses.
    """
    resp = _tcp_send_raw(command, params or {})

    # Extract _debug before serializing, prepend as visible header
    debug = resp.pop("_debug", None)
    body = json.dumps(resp, default=str, indent=2)

    if debug:
        return _format_debug_header(debug) + body

    return body
