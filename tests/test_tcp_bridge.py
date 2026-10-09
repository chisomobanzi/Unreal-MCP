"""Unit tests for the TCP transport — port discovery, retry policy, diagnosis.

All tests run without an editor: sockets are monkeypatched.
"""
import os
import socket
import time

import pytest

from unrealmcp import _tcp_bridge as tb


# ---------------------------------------------------------------------------
# Project root / port discovery
# ---------------------------------------------------------------------------

def test_find_project_root_walks_up(tmp_path, monkeypatch):
    root = tmp_path / "MyProj"
    sub = root / "Content" / "Stuff"
    sub.mkdir(parents=True)
    (root / "MyProj.uproject").write_text("{}")
    monkeypatch.chdir(sub)
    monkeypatch.delenv("UNREAL_MCP_PROJECT_DIR", raising=False)
    assert tb._find_project_root() == str(root)


def test_find_project_root_env_override(tmp_path, monkeypatch):
    proj = tmp_path / "EnvProj"
    proj.mkdir()
    monkeypatch.setenv("UNREAL_MCP_PROJECT_DIR", str(proj))
    assert tb._find_project_root() == str(proj)


def test_resolve_port_precedence_env_beats_file(tmp_path, monkeypatch):
    root = tmp_path / "P"
    (root / "Saved" / "UnrealMCP").mkdir(parents=True)
    (root / "P.uproject").write_text("{}")
    (root / "Saved" / "UnrealMCP" / "port.txt").write_text("55599")
    monkeypatch.chdir(root)
    monkeypatch.setenv("UNREAL_MCP_PORT", "51000")
    assert tb._resolve_port() == 51000
    monkeypatch.delenv("UNREAL_MCP_PORT")
    monkeypatch.delenv("UNREAL_MCP_PROJECT_DIR", raising=False)
    assert tb._resolve_port() == 55599


def test_resolve_port_default_when_nothing(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("UNREAL_MCP_PORT", raising=False)
    monkeypatch.delenv("UNREAL_MCP_PROJECT_DIR", raising=False)
    assert tb._resolve_port() == tb.DEFAULT_PORT


# ---------------------------------------------------------------------------
# Connection-failure diagnosis
# ---------------------------------------------------------------------------

def _mk_project(tmp_path):
    root = tmp_path / "Proj"
    (root / "Saved" / "UnrealMCP").mkdir(parents=True)
    (root / "Proj.uproject").write_text("{}")
    return root


def test_diagnose_editor_not_running(tmp_path, monkeypatch):
    root = _mk_project(tmp_path)
    monkeypatch.chdir(root)
    monkeypatch.delenv("UNREAL_MCP_PROJECT_DIR", raising=False)
    msg = tb._diagnose_connection_failure(55557)
    assert "not running" in msg and "launch-editor" in msg


def test_diagnose_editor_crashed(tmp_path, monkeypatch):
    root = _mk_project(tmp_path)
    port_file = root / "Saved" / "UnrealMCP" / "port.txt"
    port_file.write_text("55557")
    old = time.time() - 3600
    os.utime(port_file, (old, old))
    crash = root / "Saved" / "Crashes" / "crashinfo-Test-pid-1-ABC"
    crash.mkdir(parents=True)
    (crash / "Test.log").write_text("blah\nAssertion failed: !Broken [File:X.cpp]\n")
    monkeypatch.chdir(root)
    monkeypatch.delenv("UNREAL_MCP_PROJECT_DIR", raising=False)
    msg = tb._diagnose_connection_failure(55557)
    assert "CRASHED" in msg and "Assertion failed: !Broken" in msg


def test_diagnose_stale_port_file(tmp_path, monkeypatch):
    root = _mk_project(tmp_path)
    (root / "Saved" / "UnrealMCP" / "port.txt").write_text("55557")
    monkeypatch.chdir(root)
    monkeypatch.delenv("UNREAL_MCP_PROJECT_DIR", raising=False)
    msg = tb._diagnose_connection_failure(55557)
    assert "port file exists" in msg


# ---------------------------------------------------------------------------
# Retry policy: never retry after the request was sent
# ---------------------------------------------------------------------------

class _SentThenTimeoutSocket:
    """Accepts the send, then times out on recv — the double-exec scenario."""
    def __init__(self, *a, **k): pass
    def settimeout(self, t): pass
    def setsockopt(self, *a): pass
    def connect(self, addr): pass
    def sendall(self, data): pass
    def recv(self, n): raise socket.timeout("recv timed out")
    def shutdown(self, how): pass
    def close(self): pass


def test_no_retry_after_send(monkeypatch):
    monkeypatch.setattr(socket, "socket", _SentThenTimeoutSocket)
    # After a lost reply the client polls the bridge journal (get_command_result)
    # until a 15 s time.monotonic() deadline. A no-op sleep alone would spin
    # that loop for 15 real seconds, so sleep advances a fake clock instead.
    clock = {"t": 0.0}
    monkeypatch.setattr(time, "monotonic", lambda: clock["t"])
    monkeypatch.setattr(time, "sleep", lambda s: clock.__setitem__("t", clock["t"] + s))
    conn = tb._TCPConnection()
    conn._port = 51234  # skip discovery
    sends = {}
    orig = tb._TCPConnection._send_once
    def counting_send_once(self, command, params):
        sends[command] = sends.get(command, 0) + 1
        return orig(self, command, params)
    monkeypatch.setattr(tb._TCPConnection, "_send_once", counting_send_once)

    resp = conn.send_command("spawn_actor", {"name": "X"})
    assert resp["status"] == "error"
    assert "NOT retrying" in resp["error"] or "not retrying" in resp["error"].lower()
    assert sends.get("spawn_actor") == 1, "post-send failure must not be retried"
    # It asks the journal whether the command finished instead of re-sending it;
    # this socket never answers, so the original error stands.
    assert sends.get("get_command_result", 0) >= 1, "lost reply should consult the journal"


class _RefusingSocket:
    def __init__(self, *a, **k): pass
    def settimeout(self, t): pass
    def setsockopt(self, *a): pass
    def connect(self, addr): raise ConnectionRefusedError("refused")
    def shutdown(self, how): pass
    def close(self): pass


def test_connect_failures_are_retried(monkeypatch, tmp_path):
    monkeypatch.chdir(tmp_path)  # no project -> no crash-dir scanning surprises
    monkeypatch.delenv("UNREAL_MCP_PROJECT_DIR", raising=False)
    monkeypatch.setattr(socket, "socket", _RefusingSocket)
    monkeypatch.setattr(time, "sleep", lambda s: None)
    conn = tb._TCPConnection()
    attempts = {"n": 0}
    orig = tb._TCPConnection._send_once
    def counting_send_once(self, command, params):
        attempts["n"] += 1
        return orig(self, command, params)
    monkeypatch.setattr(tb._TCPConnection, "_send_once", counting_send_once)

    resp = conn.send_command("health_check")
    assert resp["status"] == "error"
    assert attempts["n"] == tb.MAX_RETRIES + 1, "pre-send failures should retry"
