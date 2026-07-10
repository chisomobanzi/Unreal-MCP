#!/usr/bin/env python3
"""Phase 1c drill: kill the editor mid-op → watchdog recovers → work continues.

DESTRUCTIVE BY DESIGN: SIGKILLs the running editor (the 'forced crash' from
the acceptance criteria). Refuses to run if a cook/UAT process is active.

Checks:
  1x healthy baseline (get_editor_state → running_ready / plan none)
  2x kill mid-op → classified crashed/not_running, plan relaunch
  3x recover_editor → relaunch, ready again, structured event
  4x bridge actually works after recovery (round-trip command)
"""
import glob
import json
import os
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from unrealmcp import _watchdog as wd  # noqa: E402


PASS, FAIL = 0, 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"PASS  {name}")
    else:
        FAIL += 1
        print(f"FAIL  {name}  ->  {str(detail)[:300]}")


def main():
    probes = wd.collect_probes()
    if probes["cook_pids"]:
        print("ABORT: cook/UAT running — not killing anything")
        sys.exit(2)

    st = wd.get_editor_state()
    check("1.1 baseline running_ready", st["state"] == "running_ready", st)
    check("1.2 baseline plan none", st["plan"]["action"] == "none", st["plan"])
    if st["state"] != "running_ready":
        print("ABORT: need a healthy editor to start the drill")
        sys.exit(2)

    pids = st["editor_pids"]
    check("1.3 editor pid known", bool(pids), pids)

    # Fire a slow-ish command and kill the editor while it's in flight.
    import socket
    import struct
    port = int(open(probes["port_file"]).read().strip())
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.settimeout(3)
    payload = json.dumps({"type": "execute_python",
                          "params": {"code": "import time\ntime.sleep(30)\nresult='never'"}}).encode()
    s.sendall(struct.pack(">I", len(payload)) + payload)
    time.sleep(1.5)  # let it dispatch to the game thread

    for pid in pids:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
    print("      (editor SIGKILLed mid-op)")
    try:
        s.recv(4)
    except Exception:
        pass
    s.close()
    time.sleep(3)

    st2 = wd.get_editor_state()
    check("2.1 classified crashed or not_running",
          st2["state"] in ("crashed", "not_running"), st2)
    check("2.2 plan is relaunch", st2["plan"]["action"] == "relaunch", st2["plan"])

    t0 = time.monotonic()
    ev = wd.recover_editor(wait_timeout_s=600)
    check("3.1 recovery event ok", ev.get("ok") is True, ev)
    check("3.2 action relaunch", ev.get("action") == "relaunch", ev)
    check("3.3 event shape (crashed_recovered + downtime)",
          ev.get("event") == "crashed_recovered"
          and isinstance(ev.get("downtime_s"), (int, float)), ev)
    print(f"      (recovered in {ev.get('downtime_s', '?')}s)")

    st3 = wd.get_editor_state()
    check("3.4 running_ready after recovery", st3["state"] == "running_ready", st3)

    # 4. bridge actually usable again
    try:
        port = int(open(probes["port_file"]).read().strip())
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
        s.settimeout(60)
        payload = json.dumps({"type": "get_world_info", "params": {}}).encode()
        s.sendall(struct.pack(">I", len(payload)) + payload)
        hdr = b""
        while len(hdr) < 4:
            hdr += s.recv(4 - len(hdr))
        ln = struct.unpack(">I", hdr)[0]
        buf = b""
        while len(buf) < ln:
            buf += s.recv(ln - len(buf))
        s.close()
        resp = json.loads(buf.decode())
        ok = resp.get("status") == "success"
    except Exception as exc:
        ok, resp = False, str(exc)
    check("4.1 command round-trips after recovery", ok, resp)

    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
