"""Resumable batch runner: checkpointing, crash recovery, resume (editor-less)."""
import json
import os

import pytest

from unrealmcp import _batch


def make_send(crash_on=None, crashed_flag=None):
    """Fake _tcp_send_raw. Crashes (raises) the first time item `crash_on`
    is attempted; records calls."""
    calls = []
    state = {"crashed_once": False}

    def send(command, params=None):
        item = (params or {}).get("code", "")
        calls.append(item)
        if crash_on is not None and crash_on in item and not state["crashed_once"]:
            state["crashed_once"] = True
            if crashed_flag is not None:
                crashed_flag["down"] = True
            raise ConnectionError("editor died")
        if crashed_flag is not None and crashed_flag.get("down"):
            raise ConnectionError("editor still down")
        return {"status": "success", "result": {"ok": True, "item": item}}

    send.calls = calls
    return send


def make_recover(crashed_flag):
    events = []

    def recover(**kw):
        crashed_flag["down"] = False
        ev = {"event": "crashed_recovered", "ok": True, "action": "relaunch",
              "downtime_s": 1.0}
        events.append(ev)
        return ev

    recover.events = events
    return recover


def items_for(n):
    return [{"code": f"item-{i}"} for i in range(n)]


def test_happy_path_all_items(tmp_path):
    send = make_send()
    res = _batch.run_batch("execute_python", items_for(3), batch_id="b1",
                           checkpoint_dir=str(tmp_path), send=send,
                           recover=lambda **kw: None)
    assert res["completed"] == 3 and res["failed"] == 0
    assert len(send.calls) == 3


def test_checkpoint_written_per_item(tmp_path):
    send = make_send()
    _batch.run_batch("execute_python", items_for(2), batch_id="b2",
                     checkpoint_dir=str(tmp_path), send=send,
                     recover=lambda **kw: None)
    ck = json.load(open(os.path.join(str(tmp_path), "b2.json")))
    assert ck["command"] == "execute_python"
    assert sorted(ck["done"]) == [0, 1]


def test_crash_mid_batch_recovers_and_continues(tmp_path):
    crashed = {"down": False}
    send = make_send(crash_on="item-1", crashed_flag=crashed)
    recover = make_recover(crashed)
    res = _batch.run_batch("execute_python", items_for(3), batch_id="b3",
                           checkpoint_dir=str(tmp_path), send=send,
                           recover=recover)
    assert len(recover.events) == 1          # watchdog invoked once
    assert res["completed"] == 3             # item-1 retried after recovery
    assert res["recoveries"] == 1
    assert res["failed"] == 0


def test_resume_skips_done_items(tmp_path):
    # First run: crashes on item-1 and recovery FAILS -> batch stops.
    crashed = {"down": False}
    send = make_send(crash_on="item-1", crashed_flag=crashed)

    def bad_recover(**kw):
        return {"event": "recovery_failed", "ok": False}

    res1 = _batch.run_batch("execute_python", items_for(3), batch_id="b4",
                            checkpoint_dir=str(tmp_path), send=send,
                            recover=bad_recover)
    assert res1["completed"] == 1
    assert res1["aborted"] is True

    # Second run, same batch_id: resumes at item-1, finishes the rest.
    crashed["down"] = False
    send2 = make_send()
    res2 = _batch.run_batch("execute_python", items_for(3), batch_id="b4",
                            checkpoint_dir=str(tmp_path), send=send2,
                            recover=lambda **kw: None)
    assert res2["completed"] == 3
    assert "item-0" not in send2.calls       # item 0 was checkpointed done
    assert res2["resumed_from"] == 1


def test_item_error_response_is_failure_not_crash(tmp_path):
    def send(command, params=None):
        return {"status": "error", "error": "bad params"}

    res = _batch.run_batch("execute_python", items_for(2), batch_id="b5",
                           checkpoint_dir=str(tmp_path), send=send,
                           # editor is healthy: recover must not run
                           state_probe=lambda: {"state": "running_ready"},
                           recover=lambda **kw: pytest.fail("recover must not run"))
    assert res["completed"] == 0 and res["failed"] == 2
    assert res["aborted"] is False           # tool errors don't stop the batch


def test_error_response_with_dead_editor_triggers_recovery(tmp_path):
    # Live gap: the transport does NOT raise on a mid-item editor crash —
    # it returns a post-send error dict ("delivered but no response").
    # The batch must probe the editor state and recover, not just record
    # a tool failure.
    crashed = {"down": True}
    calls = []

    def send(command, params=None):
        item = (params or {}).get("code", "")
        calls.append(item)
        if crashed["down"]:
            return {"status": "error",
                    "error": "'execute_python' was delivered to the editor "
                             "but no response arrived (timed out)."}
        return {"status": "success", "result": {"ok": True}}

    def state_probe():
        return {"state": "crashed" if crashed["down"] else "running_ready"}

    recover = make_recover(crashed)
    res = _batch.run_batch("execute_python", items_for(2), batch_id="b6",
                           checkpoint_dir=str(tmp_path), send=send,
                           state_probe=state_probe, recover=recover)
    assert len(recover.events) == 1
    assert res["completed"] == 2
    assert res["recoveries"] == 1
    assert res["failed"] == 0
