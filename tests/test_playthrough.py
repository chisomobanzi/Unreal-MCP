"""Playthrough flow runner: beat sequencing, asserts, crash-restart (editor-less)."""
import json

import pytest

from unrealmcp import _playthrough as pt


def make_actions(fail_on=None, crash_on=None, log=None):
    calls = log if log is not None else []
    state = {"crashed_once": False}

    def action(name):
        def run(args):
            calls.append((name, args))
            if crash_on == name and not state["crashed_once"]:
                state["crashed_once"] = True
                raise ConnectionError("editor died")
            if fail_on == name:
                return {"ok": False, "error": "action failed"}
            return {"ok": True, "detail": name}
        return run

    return {n: action(n) for n in
            ("start_pie", "stop_pie", "teleport_to_room", "stand_before",
             "hand_ray_select", "interact", "wait", "capture", "exec",
             "assert_state", "load_map")}, calls


def flow(*beats):
    return {"name": "test-flow", "beats": [dict(b) for b in beats]}


def test_beats_run_in_order():
    actions, calls = make_actions()
    res = pt.run_flow(flow({"action": "start_pie"},
                           {"action": "teleport_to_room", "args": {"room": "X"}},
                           {"action": "stop_pie"}),
                      actions=actions, recover=lambda **kw: {"ok": True})
    assert res["ok"] is True
    assert [c[0] for c in calls] == ["start_pie", "teleport_to_room", "stop_pie"]
    assert len(res["beats"]) == 3
    assert all(b["ok"] for b in res["beats"])


def test_failed_beat_stops_flow():
    actions, calls = make_actions(fail_on="teleport_to_room")
    res = pt.run_flow(flow({"action": "start_pie"},
                           {"action": "teleport_to_room"},
                           {"action": "stop_pie"}),
                      actions=actions, recover=lambda **kw: {"ok": True})
    assert res["ok"] is False
    assert res["failed_beat"] == 1
    # stop_pie still runs as cleanup
    assert calls[-1][0] == "stop_pie"


def test_crash_mid_flow_recovers_and_replays_once():
    actions, calls = make_actions(crash_on="hand_ray_select")
    recoveries = []

    def recover(**kw):
        recoveries.append(1)
        return {"ok": True, "event": "crashed_recovered"}

    res = pt.run_flow(flow({"action": "start_pie"},
                           {"action": "hand_ray_select", "args": {"target": "Pod"}},
                           {"action": "stop_pie"}),
                      actions=actions, recover=recover, max_restarts=1)
    assert len(recoveries) == 1
    assert res["ok"] is True
    assert res["restarts"] == 1
    # flow replayed from the start after recovery
    names = [c[0] for c in calls]
    assert names.count("start_pie") == 2


def test_crash_with_no_restarts_left_fails():
    actions, calls = make_actions(crash_on="hand_ray_select")
    res = pt.run_flow(flow({"action": "hand_ray_select"}),
                      actions=actions,
                      recover=lambda **kw: {"ok": True}, max_restarts=0)
    assert res["ok"] is False
    assert res["crashed"] is True


def test_unknown_action_is_a_failure():
    actions, _ = make_actions()
    res = pt.run_flow(flow({"action": "definitely_not_a_thing"}),
                      actions=actions, recover=lambda **kw: {"ok": True})
    assert res["ok"] is False
    assert "unknown action" in json.dumps(res)


def test_report_is_json_serializable():
    actions, _ = make_actions()
    res = pt.run_flow(flow({"action": "start_pie"}), actions=actions,
                      recover=lambda **kw: {"ok": True})
    json.dumps(res)
