"""playthrough(flow): run a scripted VR flow unattended, beat by beat.

A flow is {"name": ..., "beats": [{"action": ..., "args": {...}}, ...]}.
Each beat runs one action; a failed beat stops the flow (cleanup beats named
stop_pie still run); a CRASH triggers the watchdog and one full replay of
the flow (game state can't be checkpointed mid-PIE, so resume = restart).

Actions (live registry): load_map, start_pie, stop_pie, wait,
get_pawn_state, teleport_to_room, move_to, stand_before, look_at,
hand_ray_select, interact, capture, exec, assert_state.

assert_state args:
  widget_text_contains: {"actor": ..., "text": ...} — a widget tree text
  log_contains: str — project log emitted since the beat started
  pawn_near: {"x","y","z","tolerance"} — pawn within tolerance of a point
  pie_active: bool
"""
from __future__ import annotations

import json
import os
import time
from typing import Any, Callable, Dict, List, Optional


# --------------------------------------------------------------------------
# Pure flow runner (unit-tested; actions injected)
# --------------------------------------------------------------------------

def run_flow(flow: Dict[str, Any],
             actions: Dict[str, Callable],
             recover: Callable,
             max_restarts: int = 1) -> Dict[str, Any]:
    name = flow.get("name", "unnamed-flow")
    beats: List[Dict[str, Any]] = flow.get("beats", [])
    restarts = 0

    while True:
        report: List[Dict[str, Any]] = []
        failed_beat: Optional[int] = None
        crashed = False

        for idx, beat in enumerate(beats):
            action = beat.get("action", "")
            args = beat.get("args", {}) or {}
            entry: Dict[str, Any] = {"index": idx, "action": action}
            fn = actions.get(action)
            if fn is None:
                entry.update(ok=False, error=f"unknown action: {action}")
                report.append(entry)
                failed_beat = idx
                break
            t0 = time.monotonic()
            try:
                result = fn(args) or {}
            except Exception as exc:
                entry.update(ok=False, error=f"crash: {exc}")
                report.append(entry)
                crashed = True
                failed_beat = idx
                break
            entry["seconds"] = round(time.monotonic() - t0, 1)
            entry["ok"] = bool(result.get("ok", True))
            detail = {k: v for k, v in result.items() if k != "ok"}
            if detail:
                entry["detail"] = detail
            report.append(entry)
            if not entry["ok"]:
                failed_beat = idx
                break

        if failed_beat is not None and not crashed:
            # Run trailing stop_pie beats as cleanup so PIE doesn't leak.
            for idx in range(failed_beat + 1, len(beats)):
                if beats[idx].get("action") == "stop_pie":
                    try:
                        actions["stop_pie"](beats[idx].get("args", {}) or {})
                        report.append({"index": idx, "action": "stop_pie",
                                       "ok": True, "cleanup": True})
                    except Exception:
                        pass

        if crashed and restarts < max_restarts:
            ev = recover() or {}
            if ev.get("ok", False):
                restarts += 1
                continue  # replay the whole flow
            return {"ok": False, "flow": name, "crashed": True,
                    "recovery_failed": True, "recovery": ev,
                    "restarts": restarts, "beats": report,
                    "failed_beat": failed_beat}

        return {"ok": failed_beat is None, "flow": name,
                "beats": report, "failed_beat": failed_beat,
                "crashed": crashed, "restarts": restarts}


# --------------------------------------------------------------------------
# Live action registry
# --------------------------------------------------------------------------

def _call(cmd: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    from unrealmcp._tcp_bridge import _tcp_send_raw
    resp = _tcp_send_raw(cmd, params)
    if resp.get("status") == "error":
        err = str(resp.get("error", ""))
        if "delivered to the editor but no response" in err \
                or "Cannot connect" in err:
            # Let the flow runner treat transport loss as a crash.
            from unrealmcp._watchdog import get_editor_state
            st = get_editor_state()
            if st.get("state") in ("crashed", "not_running"):
                raise ConnectionError(err)
        return {"ok": False, "error": err}
    out = resp.get("result", {})
    if isinstance(out, dict):
        out.setdefault("ok", out.get("success", True))
        return out
    return {"ok": True, "result": out}


def _log_path() -> str:
    from unrealmcp._tcp_bridge import _find_project_root
    root = _find_project_root() or os.getcwd()
    name = os.path.basename(glob_first(os.path.join(root, "*.uproject")))
    return os.path.join(root, "Saved", "Logs",
                        name.replace(".uproject", "") + ".log")


def glob_first(pattern: str) -> str:
    import glob as _glob
    hits = _glob.glob(pattern)
    return hits[0] if hits else pattern


def live_actions() -> Dict[str, Callable]:
    from unrealmcp import _player

    log_offset = {"pos": 0}

    def _beat_log_mark():
        try:
            log_offset["pos"] = os.path.getsize(_log_path())
        except OSError:
            log_offset["pos"] = 0

    def load_map(args):
        pkg = args["map"]
        _call("execute_python", {"code": f"""
import unreal
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("{pkg}")
result = "issued"
"""})
        time.sleep(3)
        w = _call("get_world_info", {})
        ok = pkg.endswith(str(w.get("world", "")))
        return {"ok": ok, "world": w.get("world")}

    def start_pie(args):
        _call("start_pie", args)
        deadline = time.monotonic() + args.get("timeout", 90)
        while time.monotonic() < deadline:
            if _call("get_pie_status", {}).get("pie_active"):
                time.sleep(args.get("settle", 5))
                return {"ok": True}
            time.sleep(2)
        return {"ok": False, "error": "PIE did not start"}

    def stop_pie(args):
        _call("stop_pie", {})
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if not _call("get_pie_status", {}).get("pie_active"):
                return {"ok": True}
            time.sleep(1)
        return {"ok": False, "error": "PIE did not stop"}

    def wait(args):
        time.sleep(float(args.get("seconds", 1)))
        return {"ok": True}

    def capture(args):
        r = _call("take_screenshot", {"max_width": int(args.get("max_width", 800))})
        path = r.get("file_path", "")
        return {"ok": bool(path) and os.path.exists(path), "file": path}

    def exec_py(args):
        from unrealmcp._player import _exec
        return _exec(args.get("code", "result='noop'"))

    def raw(args):
        return _call(args.get("command", "ping"), args.get("params", {}) or {})

    def assert_state(args):
        _ = args
        checks = []
        if "pie_active" in args:
            st = _call("get_pie_status", {})
            checks.append(("pie_active",
                           bool(st.get("pie_active")) == bool(args["pie_active"]),
                           st))
        if "pawn_near" in args:
            spec = args["pawn_near"]
            st = _player.get_pawn_state()
            loc = st.get("location", [1e9, 1e9, 1e9])
            tol = float(spec.get("tolerance", 300))
            ok = (abs(loc[0] - spec["x"]) < tol and abs(loc[1] - spec["y"]) < tol)
            checks.append(("pawn_near", ok, {"pawn": loc}))
        if "widget_text_contains" in args:
            spec = args["widget_text_contains"]
            tree = _player.get_widget_tree(actor=spec["actor"])
            texts = [e.get("text", "") for wc in tree.get("widget_components", [])
                     for e in wc.get("tree", []) if e.get("text")]
            ok = any(spec["text"].lower() in t.lower() for t in texts)
            checks.append(("widget_text_contains", ok, {"texts": texts[:10]}))
        if "log_contains" in args:
            needle = args["log_contains"]
            try:
                with open(_log_path(), "rb") as f:
                    f.seek(log_offset["pos"])
                    data = f.read().decode("utf-8", errors="replace")
                ok = needle in data
            except OSError:
                ok = False
            checks.append(("log_contains", ok, {"needle": needle}))
        failed = [c for c in checks if not c[1]]
        return {"ok": not failed,
                "checks": [{"check": c[0], "ok": c[1],
                            **({"detail": c[2]} if not c[1] else {})}
                           for c in checks],
                **({"error": f"{len(failed)} assertion(s) failed"} if failed else {})}

    def crash_aware(fn):
        # Most transport layers return error DICTS on a dead editor (the
        # no-double-exec policy), so a mid-beat crash surfaces as a failed
        # beat. Reclassify: any failed beat with a crashed/gone editor is
        # raised as ConnectionError so the flow runner recovers + replays.
        def run(args):
            result = fn(args)
            if isinstance(result, dict) and not result.get("ok", True):
                from unrealmcp._watchdog import get_editor_state
                st = get_editor_state()
                if st.get("state") in ("crashed", "not_running"):
                    raise ConnectionError(result.get("error", "editor crashed"))
            return result
        return run

    def wrap_player(fn):
        def run(args):
            _beat_log_mark()
            return fn(**args)
        return crash_aware(run)

    def marked(fn):
        def run(args):
            _beat_log_mark()
            return fn(args)
        return crash_aware(run)

    return {
        "load_map": marked(load_map),
        "start_pie": marked(start_pie),
        "stop_pie": marked(stop_pie),
        "wait": marked(wait),
        "capture": marked(capture),
        "exec": marked(exec_py),
        "raw": marked(raw),
        # no mark: asserts SINCE the last action
        "assert_state": crash_aware(assert_state),
        "get_pawn_state": wrap_player(_player.get_pawn_state),
        "teleport_to_room": wrap_player(_player.teleport_to_room),
        "move_to": wrap_player(_player.move_to),
        "look_at": wrap_player(_player.look_at),
        "stand_before": wrap_player(_player.stand_before),
        "hand_ray_select": wrap_player(_player.hand_ray_select),
        "interact": wrap_player(_player.interact),
    }


def run_playthrough(flow: Dict[str, Any], max_restarts: int = 1) -> Dict[str, Any]:
    """Live entry point: unattended mode on, run the flow with watchdog
    recovery, restore unattended mode, attach bridge events."""
    from unrealmcp._watchdog import recover_editor

    def _recover(**kw):
        ev = recover_editor(**kw)
        if ev.get("ok"):
            # A relaunched editor boots attended — restore the mode the
            # replayed beats rely on.
            try:
                _call("set_unattended_mode", {"enabled": True})
            except Exception:
                pass
        return ev

    _call("set_unattended_mode", {"enabled": True})
    _, seq0 = _events_tail(0)
    try:
        result = run_flow(flow, live_actions(), _recover,
                          max_restarts=max_restarts)
    finally:
        try:
            _call("set_unattended_mode", {"enabled": False})
        except Exception:
            pass  # editor may be down after a failed recovery
    try:
        events, _ = _events_tail(seq0)
        result["bridge_events"] = events
    except Exception:
        result["bridge_events"] = []
    return result


def _events_tail(since: float):
    r = _call("get_bridge_events", {"since": since})
    return r.get("events", []), r.get("last_seq", since)
