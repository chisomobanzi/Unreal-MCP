#!/usr/bin/env python3
"""Phase 1d drill: crash-resumable batch, live.

DESTRUCTIVE BY DESIGN: item 2 segfaults the editor (NULL read — the same
failure shape as an OOM kill) on its first attempt. The batch must
checkpoint, the watchdog must relaunch the editor, the item must succeed on
retry, and the remaining items must complete. Refuses during cooks.
"""
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from unrealmcp import _batch, _watchdog  # noqa: E402

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
    probes = _watchdog.collect_probes()
    if probes["cook_pids"]:
        print("ABORT: cook/UAT running")
        sys.exit(2)
    st = _watchdog.get_editor_state()
    if st["state"] != "running_ready":
        print(f"ABORT: editor not ready ({st['state']})")
        sys.exit(2)

    flag = os.path.join(tempfile.gettempdir(), f"mcp_batch_crash_{int(time.time())}")
    crash_code = f"""
import os
if not os.path.exists({flag!r}):
    open({flag!r}, "w").write("x")
    import ctypes
    ctypes.string_at(0)   # segfault — simulates OOM death mid-item
result = "survived-after-recovery"
"""
    items = [
        {"code": "result = 'item-0-ok'"},
        {"code": "result = 'item-1-ok'"},
        {"code": crash_code},
        {"code": "result = 'item-3-ok'"},
    ]

    batch_id = f"drill-{int(time.time())}"
    t0 = time.monotonic()
    res = _batch.run_batch("execute_python", items, batch_id=batch_id)
    dt = time.monotonic() - t0

    check("1.1 batch completed all items", res["completed"] == 4, res)
    check("1.2 exactly one recovery", res["recoveries"] == 1, res)
    check("1.3 no tool-level failures", res["failed"] == 0, res)
    check("1.4 not aborted", res["aborted"] is False, res)
    check("1.5 checkpoint file exists", os.path.exists(res["checkpoint"]), res["checkpoint"])
    crash_result = res["results"].get("2", {})
    check("1.6 crashed item succeeded on retry",
          crash_result.get("ok") and "survived-after-recovery" in str(crash_result),
          crash_result)
    print(f"      (batch of 4 with mid-batch editor crash: {dt:.0f}s)")

    st = _watchdog.get_editor_state()
    check("1.7 editor healthy after batch", st["state"] == "running_ready", st)

    try:
        os.remove(flag)
    except OSError:
        pass
    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
