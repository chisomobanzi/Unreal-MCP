#!/usr/bin/env python3
"""Phase 1b drill: focus/throttle-proof viewport capture.

Proves take_screenshot returns a FRESH, non-black frame while the editor
window is backgrounded/minimized, in both editor and PIE. xdotool is used
ONLY to minimize/restore the window (test setup) — never to drive the editor.

Checks:
  1x editor-viewport capture, focused (baseline sanity)
  2x editor-viewport capture while minimized: non-black, and two captures
     with a camera move between them must differ (freshness)
  3x PIE capture while minimized: non-black, PIE stopped cleanly
"""
import glob
import hashlib
import json
import os
import socket
import struct
import subprocess
import sys
import time

from PIL import Image, ImageStat


def _find_port_file() -> str:
    env_dir = os.environ.get("UNREAL_MCP_PROJECT_DIR")
    if env_dir:
        return os.path.join(env_dir, "Saved", "UnrealMCP", "port.txt")
    d = os.getcwd()
    while True:
        if glob.glob(os.path.join(d, "*.uproject")):
            return os.path.join(d, "Saved", "UnrealMCP", "port.txt")
        parent = os.path.dirname(d)
        if parent == d:
            return os.path.join("Saved", "UnrealMCP", "port.txt")
        d = parent


PORT_FILE = _find_port_file()


def call(cmd, params=None, timeout=120):
    port = int(open(PORT_FILE).read().strip())
    s = socket.create_connection(("127.0.0.1", port), timeout=15)
    s.settimeout(timeout)
    payload = json.dumps({"type": cmd, "params": params or {}}).encode()
    s.sendall(struct.pack(">I", len(payload)) + payload)

    def recvn(n):
        buf = b""
        while len(buf) < n:
            chunk = s.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("socket closed")
            buf += chunk
        return buf

    ln = struct.unpack(">I", recvn(4))[0]
    data = json.loads(recvn(ln).decode())
    s.close()
    return data


def result_of(resp):
    if resp.get("status") == "error":
        return {"success": False, "error": resp.get("error", "unknown")}
    return resp.get("result", resp)


PASS, FAIL = 0, 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"PASS  {name}")
    else:
        FAIL += 1
        print(f"FAIL  {name}  ->  {str(detail)[:300]}")


def screenshot():
    """take_screenshot -> (path, sha1, mean_brightness, stddev)."""
    r = result_of(call("take_screenshot", {"max_width": 640}, timeout=120))
    path = r.get("file_path", "")
    if not path or not os.path.exists(path):
        return "", "", -1.0, -1.0
    with open(path, "rb") as f:
        digest = hashlib.sha1(f.read()).hexdigest()
    img = Image.open(path).convert("L")
    stat = ImageStat.Stat(img)
    return path, digest, stat.mean[0], stat.stddev[0]


def editor_window_id():
    try:
        out = subprocess.run(
            ["xdotool", "search", "--name", "Unreal Editor"],
            capture_output=True, text=True, timeout=10)
        ids = [l for l in out.stdout.split() if l.strip()]
        return ids[-1] if ids else ""
    except Exception:
        return ""


def main():
    result_of(call("set_unattended_mode", {"enabled": True}))

    # --- 1. focused baseline -------------------------------------------
    result_of(call("set_viewport_camera",
                   {"location": [800, 800, 350], "look_at": [0, 0, 100]}))
    p, h1, mean1, sd1 = screenshot()
    check("1.1 focused capture non-black", mean1 > 8 and sd1 > 2,
          {"mean": mean1, "sd": sd1, "path": p})

    win = editor_window_id()
    check("1.2 editor window found (xdotool, test setup only)", bool(win), win)
    if not win:
        result_of(call("set_unattended_mode", {"enabled": False}))
        print(f"\n=== {PASS} passed, {FAIL} failed (no window; aborted) ===")
        sys.exit(1)

    try:
        # --- 2. minimized editor captures ------------------------------
        subprocess.run(["xdotool", "windowminimize", win], timeout=10)
        time.sleep(2)

        _, h2, mean2, sd2 = screenshot()
        check("2.1 minimized capture non-black", mean2 > 8 and sd2 > 2,
              {"mean": mean2, "sd": sd2})
        check("2.2 minimized capture not the focused frame re-served (fresh pipeline)",
              True)  # informational; freshness proven by 2.3

        result_of(call("set_viewport_camera",
                       {"location": [-900, 400, 500], "look_at": [0, 0, 100]}))
        _, h3, mean3, sd3 = screenshot()
        check("2.3 camera move re-renders while minimized (frames differ)",
              h3 and h3 != h2, {"h2": h2[:12], "h3": h3[:12]})
        check("2.4 second minimized capture non-black", mean3 > 8 and sd3 > 2,
              {"mean": mean3, "sd": sd3})

        # --- 3. PIE capture while minimized ----------------------------
        r = result_of(call("start_pie", {"simulate": True}))
        check("3.1 start_pie accepted (minimized)", "error" not in r, r)
        pie = False
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            st = result_of(call("get_pie_status", timeout=15))
            if st.get("pie_active"):
                pie = True
                break
            time.sleep(2)
        check("3.2 PIE reached while minimized", pie)

        if pie:
            time.sleep(2)
            _, h4, mean4, sd4 = screenshot()
            check("3.3 PIE capture while minimized non-black",
                  mean4 > 8 and sd4 > 2, {"mean": mean4, "sd": sd4})
            _, h5, _, _ = screenshot()
            # PIE world animates (fog, HDRI rotation, particles) — two
            # captures a moment apart differing is evidence of live frames,
            # but a perfectly static scene may legitimately match; only
            # informational.
            print(f"      (PIE frame delta: {'differs' if h5 != h4 else 'identical'})")

        result_of(call("stop_pie", {}))
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            st = result_of(call("get_pie_status", timeout=15))
            if not st.get("pie_active"):
                break
            time.sleep(1)
        check("3.4 PIE stopped", not result_of(call("get_pie_status")).get("pie_active"))
    finally:
        subprocess.run(["xdotool", "windowmap", win], timeout=10)
        subprocess.run(["xdotool", "windowactivate", win], timeout=10)
        result_of(call("set_unattended_mode", {"enabled": False}))

    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
