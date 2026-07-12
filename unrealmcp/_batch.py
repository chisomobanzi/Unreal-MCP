"""Resumable batch runner: per-item checkpointing + crash auto-recovery.

Runs one bridge command per item. After every item the checkpoint file is
rewritten, so a crash (editor OOM, SIGSEGV) loses at most the in-flight
item. Connection failures trigger the watchdog (recover_editor) and one
retry of the failed item; tool-level errors are recorded and the batch
continues. Re-running with the same batch_id resumes from the checkpoint.
"""
from __future__ import annotations

import json
import os
import time
from typing import Any, Callable, Dict, List, Optional


def _default_send(command: str, params: Optional[Dict[str, Any]] = None):
    from unrealmcp._tcp_bridge import _tcp_send_raw
    return _tcp_send_raw(command, params)


def _default_recover(**kw):
    from unrealmcp._watchdog import recover_editor
    return recover_editor(**kw)


def _default_checkpoint_dir() -> str:
    from unrealmcp._tcp_bridge import _find_project_root
    root = _find_project_root() or os.getcwd()
    return os.path.join(root, "Saved", "UnrealMCP", "batches")


def _default_state_probe() -> Dict[str, Any]:
    from unrealmcp._watchdog import get_editor_state
    return get_editor_state()


def run_batch(command: str,
              items: List[Dict[str, Any]],
              batch_id: str,
              checkpoint: bool = True,
              checkpoint_dir: Optional[str] = None,
              send: Optional[Callable] = None,
              recover: Optional[Callable] = None,
              state_probe: Optional[Callable] = None) -> Dict[str, Any]:
    """Run `command` once per item (each item = its params dict)."""
    send = send or _default_send
    recover = recover or _default_recover
    state_probe = state_probe or _default_state_probe
    ck_dir = checkpoint_dir or _default_checkpoint_dir()
    ck_path = os.path.join(ck_dir, f"{batch_id}.json")

    state: Dict[str, Any] = {
        "batch_id": batch_id,
        "command": command,
        "total": len(items),
        "done": [],
        "results": {},
    }
    resumed_from = 0
    if checkpoint and os.path.exists(ck_path):
        try:
            prior = json.load(open(ck_path))
            if prior.get("command") == command and prior.get("total") == len(items):
                state = prior
                done = set(state.get("done", []))
                resumed_from = min((i for i in range(len(items)) if i not in done),
                                   default=len(items))
        except Exception:
            pass  # unreadable checkpoint: start fresh

    def save():
        if not checkpoint:
            return
        os.makedirs(ck_dir, exist_ok=True)
        tmp = ck_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(state, f)
        os.replace(tmp, ck_path)

    done = set(state.get("done", []))
    recoveries = 0
    aborted = False

    for idx, params in enumerate(items):
        if idx in done:
            continue

        result: Dict[str, Any]
        for attempt in (1, 2):
            crash_suspected = False
            error_text = ""
            try:
                resp = send(command, params)
            except Exception as exc:
                crash_suspected = True
                error_text = str(exc)
                resp = None

            if resp is not None:
                if resp.get("status") == "error":
                    # The transport deliberately returns (not raises) on
                    # post-send timeouts and connect failures. Whether this
                    # is a tool error or a dead editor is decided by probing
                    # the editor, not by string-matching the message.
                    error_text = resp.get("error", "unknown")
                    try:
                        editor = state_probe() or {}
                    except Exception:
                        editor = {}
                    crash_suspected = editor.get("state") in ("crashed", "not_running")
                    if not crash_suspected:
                        result = {"ok": False, "error": error_text}
                        break
                else:
                    result = {"ok": True, "result": resp.get("result")}
                    break

            # crash_suspected: recover once, then retry this item a single time.
            if attempt == 1:
                ev = recover() or {}
                recoveries += 1
                if not ev.get("ok", False):
                    result = {"ok": False,
                              "error": f"crash + recovery failed: {error_text}",
                              "recovery": ev}
                    aborted = True
                    break
                continue
            result = {"ok": False, "error": f"still failing after recovery: {error_text}"}
            aborted = True
            break

        state["results"][str(idx)] = result
        if result.get("ok"):
            state.setdefault("done", []).append(idx)
            done.add(idx)
        elif not aborted:
            # tool-level failure: recorded, batch continues, item NOT done
            pass
        save()
        if aborted:
            break

    # failed = tool-level failures; the crash that aborted the batch is
    # reported via `aborted`, not as a per-item failure.
    failed = sum(
        1 for r in state["results"].values()
        if not r.get("ok") and "recovery" not in r
        and "still failing" not in str(r.get("error", "")))
    return {
        "batch_id": batch_id,
        "total": len(items),
        "completed": len(done),
        "failed": failed,
        "aborted": aborted,
        "recoveries": recoveries,
        "resumed_from": resumed_from,
        "checkpoint": ck_path if checkpoint else "",
        "results": state["results"],
    }
