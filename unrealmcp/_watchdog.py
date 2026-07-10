"""Editor crash watchdog: state classification, recovery planning, recovery.

Pure logic (classify / plan_recovery / recovery_event) is separated from the
probe collectors so it is unit-testable without an editor. The live entry
points are get_editor_state() and recover_editor().

States:
  running_ready   — bridge answers, editor ready
  running_booting — bridge answers, still booting (wait, don't touch)
  hung_modal      — bridge answers but a modal window is up (engine tick
                    blocked); recover over the bridge, never by relaunch
  crashed         — fresh crash dir and/or crash-handler remnant pids
  not_running     — nothing alive, no fresh crash evidence
"""
from __future__ import annotations

import glob
import json
import os
import re
import signal
import subprocess
import time
from typing import Any, Dict, List, Optional

from unrealmcp._tcp_bridge import (
    _find_project_root,
    _latest_crash_dir,
    _crash_summary,
)

# A crash dir counts as "fresh" (explains why the editor is gone) only if it
# is newer than this many seconds; older dirs are history, not evidence. The
# watchdog runs right after a failure, so 10 minutes is generous.
CRASH_FRESH_WINDOW_S = 10 * 60


# --------------------------------------------------------------------------
# Pure logic (unit-tested)
# --------------------------------------------------------------------------

def classify(p: Dict[str, Any]) -> Dict[str, Any]:
    """Classify editor state from a probe dict (see collect_probes)."""
    st: Dict[str, Any] = {
        "editor_pids": p.get("editor_pids") or [],
        "remnant_pids": p.get("remnant_pids") or [],
        "cook_pids": p.get("cook_pids") or [],
        "port": p.get("port"),
        "listener_port": p.get("listener_port"),
        "port_file_missing": p.get("port") is None and p.get("listener_port") is not None,
    }
    health = p.get("health")
    now = p.get("now", time.time())
    crash = p.get("latest_crash")
    fresh_crash = bool(crash) and (now - crash[2]) < CRASH_FRESH_WINDOW_S

    if health:
        if health.get("modal_active"):
            st["state"] = "hung_modal"
            st["modal_title"] = health.get("modal_title", "")
        elif health.get("ready", True):
            st["state"] = "running_ready"
        else:
            st["state"] = "running_booting"
            st["not_ready_reason"] = health.get("not_ready_reason", "")
        return st

    # No bridge answer.
    if fresh_crash or st["remnant_pids"]:
        st["state"] = "crashed"
        if crash:
            st["crash_dir"] = crash[0]
            st["crash_summary"] = crash[1]
        return st

    if st["editor_pids"]:
        # Alive but not answering and no crash evidence: could be booting
        # (bridge not up yet) or wedged before the bridge started.
        st["state"] = "running_booting"
        st["not_ready_reason"] = "process alive, bridge not answering yet"
        return st

    st["state"] = "not_running"
    return st


def plan_recovery(st: Dict[str, Any]) -> Dict[str, Any]:
    """Decide the recovery action for a classified state."""
    if st.get("cook_pids"):
        return {"action": "refuse",
                "reason": "a cook/package/UAT process is running — recovery "
                          "could corrupt it; wait or stop the cook first"}

    state = st.get("state")
    if state == "hung_modal":
        return {"action": "unwedge_modal",
                "reason": "modal window up — enable unattended mode over the "
                          "fast path so the sentinel dismisses it"}
    if state in ("running_ready", "running_booting"):
        if st.get("port_file_missing"):
            return {"action": "rewrite_port_file",
                    "port": st.get("listener_port"),
                    "reason": "bridge alive but port.txt missing (cook wiped Saved/)"}
        return {"action": "none", "reason": f"editor is {state}"}
    if state in ("crashed", "not_running"):
        kill = sorted(set((st.get("editor_pids") or []) + (st.get("remnant_pids") or [])))
        return {"action": "relaunch", "kill_remnants": kill,
                "reason": st.get("crash_summary", "editor not running")}
    return {"action": "none", "reason": f"unknown state {state}"}


def recovery_event(before: Dict[str, Any], action: str, downtime_s: float,
                   ok: bool) -> Dict[str, Any]:
    """Structured crashed→recovered (or failed-recovery) event."""
    return {
        "event": "crashed_recovered" if ok else "recovery_failed",
        "action": action,
        "ok": ok,
        "downtime_s": round(float(downtime_s), 1),
        "previous_state": before.get("state", "unknown"),
        "crash_summary": before.get("crash_summary", ""),
        "crash_dir": before.get("crash_dir", ""),
        "time": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }


# --------------------------------------------------------------------------
# Live probes
# --------------------------------------------------------------------------

def _pgrep(pattern: str) -> List[int]:
    try:
        out = subprocess.run(["pgrep", "-f", pattern],
                             capture_output=True, text=True, timeout=10)
        return [int(x) for x in out.stdout.split() if x.strip().isdigit()]
    except Exception:
        return []


def _tcp_health(port: int) -> Optional[Dict[str, Any]]:
    """Raw health_check on the fast path; None if unreachable."""
    import socket
    import struct
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=3)
        s.settimeout(6)
        payload = json.dumps({"type": "health_check", "params": {}}).encode()
        s.sendall(struct.pack(">I", len(payload)) + payload)
        hdr = b""
        while len(hdr) < 4:
            c = s.recv(4 - len(hdr))
            if not c:
                return None
            hdr += c
        ln = struct.unpack(">I", hdr)[0]
        buf = b""
        while len(buf) < ln:
            c = s.recv(ln - len(buf))
            if not c:
                return None
            buf += c
        s.close()
        resp = json.loads(buf.decode())
        if resp.get("status") == "error":
            return None
        return resp.get("result", resp)
    except Exception:
        return None


def _editor_listener_port(editor_pids: List[int]) -> Optional[int]:
    """Find a 5555x LISTEN socket owned by an editor pid (ss -ltnp)."""
    if not editor_pids:
        return None
    try:
        out = subprocess.run(["ss", "-ltnp"], capture_output=True, text=True,
                             timeout=10).stdout
    except Exception:
        return None
    for line in out.splitlines():
        if "UnrealEditor" not in line:
            continue
        if not any(f"pid={pid}" in line for pid in editor_pids):
            continue
        m = re.search(r"127\.0\.0\.1:(\d{4,5})\s", line)
        if m:
            return int(m.group(1))
    return None


def collect_probes(project_dir: Optional[str] = None) -> Dict[str, Any]:
    root = project_dir or _find_project_root() or os.getcwd()
    port_file = os.path.join(root, "Saved", "UnrealMCP", "port.txt")
    port: Optional[int] = None
    try:
        port = int(open(port_file).read().strip())
    except Exception:
        port = None

    editor_pids = _pgrep(r"Binaries/Linux/UnrealEditor\s")
    remnant_pids = _pgrep(r"CrashReportClient") + _pgrep(r"apport .*UnrealEditor")
    cook_pids = (_pgrep(r"UnrealEditor-Cmd .*-run=cook")
                 + _pgrep(r"RunUAT|BuildCookRun|UnrealPak"))

    health = _tcp_health(port) if port else None
    listener = None
    if health is None and editor_pids:
        listener = _editor_listener_port(editor_pids)
        if listener:
            health = _tcp_health(listener)

    crash = None
    crash_dir = _latest_crash_dir(root)
    if crash_dir:
        try:
            mtime = os.path.getmtime(crash_dir)
            crash = (crash_dir, _crash_summary(crash_dir) or "", mtime)
        except OSError:
            crash = None

    return dict(port=port, health=health, editor_pids=editor_pids,
                remnant_pids=remnant_pids, cook_pids=cook_pids,
                listener_port=listener, latest_crash=crash, now=time.time(),
                project_dir=root, port_file=port_file)


# --------------------------------------------------------------------------
# Recovery execution
# --------------------------------------------------------------------------

def _wait_ready(project_dir: str, timeout_s: float = 600.0) -> bool:
    port_file = os.path.join(project_dir, "Saved", "UnrealMCP", "port.txt")
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        try:
            port = int(open(port_file).read().strip())
            h = _tcp_health(port)
            if h and h.get("ready", True):
                return True
        except Exception:
            pass
        time.sleep(5)
    return False


def get_editor_state(project_dir: Optional[str] = None) -> Dict[str, Any]:
    probes = collect_probes(project_dir)
    st = classify(probes)
    st["plan"] = plan_recovery(st)
    return st


def recover_editor(project_dir: Optional[str] = None,
                   wait_timeout_s: float = 600.0) -> Dict[str, Any]:
    """Classify, then execute the planned recovery. Returns the event dict."""
    probes = collect_probes(project_dir)
    root = probes["project_dir"]
    before = classify(probes)
    plan = plan_recovery(before)
    t0 = time.monotonic()

    if plan["action"] == "none":
        return recovery_event(before, "none", 0.0, ok=True)

    if plan["action"] == "refuse":
        ev = recovery_event(before, "refuse", 0.0, ok=False)
        ev["reason"] = plan["reason"]
        return ev

    if plan["action"] == "rewrite_port_file":
        port = plan.get("port")
        os.makedirs(os.path.dirname(probes["port_file"]), exist_ok=True)
        with open(probes["port_file"], "w") as f:
            f.write(str(port))
        ev = recovery_event(before, "rewrite_port_file", time.monotonic() - t0, ok=True)
        ev["port"] = port
        return ev

    if plan["action"] == "unwedge_modal":
        port = probes.get("port") or probes.get("listener_port")
        ok = False
        if port:
            import socket
            import struct
            try:
                s = socket.create_connection(("127.0.0.1", port), timeout=5)
                s.settimeout(10)
                payload = json.dumps({"type": "set_unattended_mode",
                                      "params": {"enabled": True}}).encode()
                s.sendall(struct.pack(">I", len(payload)) + payload)
                s.recv(4)
                s.close()
                ok = True
            except Exception:
                ok = False
        return recovery_event(before, "unwedge_modal", time.monotonic() - t0, ok=ok)

    # relaunch
    for pid in plan.get("kill_remnants", []):
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
    # apport may be mid-core-dump on a crashed pid; kill it too or the pid
    # lingers for minutes (bit us live).
    for pid in _pgrep(r"apport -p"):
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
    time.sleep(2)

    launcher = os.path.join(root, "launch-editor.sh")
    if not os.path.exists(launcher):
        ev = recovery_event(before, "relaunch", time.monotonic() - t0, ok=False)
        ev["reason"] = f"launcher not found: {launcher}"
        return ev

    log_path = os.path.join(root, "Saved", "Logs", "watchdog-relaunch.log")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    with open(log_path, "ab") as log:
        subprocess.Popen([launcher], cwd=root, stdout=log, stderr=log,
                         start_new_session=True)

    ok = _wait_ready(root, wait_timeout_s)
    ev = recovery_event(before, "relaunch", time.monotonic() - t0, ok=ok)
    if not ok:
        ev["reason"] = f"editor not ready within {wait_timeout_s:.0f}s (see {log_path})"
    return ev
