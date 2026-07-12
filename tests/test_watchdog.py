"""Watchdog: editor-state classification + recovery decisions (editor-less).

All probes are injected so these run without an editor, sockets, or procfs.
"""
import json
import os
import time

import pytest

from unrealmcp import _watchdog as wd


def probes(**kw):
    """Build a probe set with sane 'nothing running' defaults."""
    base = dict(
        port=None,                # int | None — parsed port.txt
        health=None,              # dict | None — health_check result via TCP
        editor_pids=[],           # live UnrealEditor pids (project match)
        remnant_pids=[],          # CrashReportClient / apport pids
        cook_pids=[],             # cook/UAT/pak pids
        listener_port=None,       # port an editor pid is LISTENing on
        latest_crash=None,        # (dir, summary, mtime) | None
        now=1000.0,
    )
    base.update(kw)
    return base


# ---- classification -------------------------------------------------------

def test_not_running():
    st = wd.classify(probes())
    assert st["state"] == "not_running"


def test_running_ready():
    st = wd.classify(probes(port=55557, editor_pids=[42],
                            health={"ready": True, "modal_active": False}))
    assert st["state"] == "running_ready"


def test_running_booting():
    st = wd.classify(probes(port=55557, editor_pids=[42],
                            health={"ready": False,
                                    "not_ready_reason": "asset registry scanning"}))
    assert st["state"] == "running_booting"


def test_hung_modal():
    st = wd.classify(probes(port=55557, editor_pids=[42],
                            health={"ready": True, "modal_active": True,
                                    "modal_title": "Save Content"}))
    assert st["state"] == "hung_modal"
    assert st["modal_title"] == "Save Content"


def test_crashed_with_fresh_crash_dir():
    st = wd.classify(probes(editor_pids=[],
                            latest_crash=("/tmp/crash-1", "Assertion failed: X", 990.0),
                            now=1000.0))
    assert st["state"] == "crashed"
    assert "Assertion failed" in st["crash_summary"]


def test_old_crash_dir_is_not_a_crash():
    # A crash dir from hours ago with no other evidence = just not running.
    st = wd.classify(probes(latest_crash=("/tmp/crash-0", "old", 1.0), now=1000.0))
    assert st["state"] == "not_running"


def test_wedged_shutdown_is_crashed():
    # Process alive + listener bound, but the bridge never answers and the
    # log ends in a crash-shutdown marker: a post-crash corpse squatting the
    # port (held one for 2 days live). Must classify crashed + kill the pid.
    st = wd.classify(probes(editor_pids=[42], listener_port=55557,
                            shutdown_marker="StaticShutdownAfterError"))
    assert st["state"] == "crashed"
    assert "StaticShutdownAfterError" in st["crash_summary"]
    d = wd.plan_recovery(st)
    assert d["action"] == "relaunch"
    assert 42 in d["kill_remnants"]


def test_booting_without_shutdown_marker_is_not_crashed():
    st = wd.classify(probes(editor_pids=[42], shutdown_marker=None))
    assert st["state"] == "running_booting"


def test_crash_remnant_pid_is_crashed_not_running():
    # Editor pid technically alive but held by CrashReportClient/apport:
    # classify as crashed, NOT running (bit us: editor-cycle waited 90s).
    st = wd.classify(probes(editor_pids=[42], remnant_pids=[43],
                            latest_crash=("/tmp/crash-2", "SIGSEGV", 995.0),
                            now=1000.0))
    assert st["state"] == "crashed"
    assert st["remnant_pids"] == [43]


def test_port_file_missing_but_listener_alive():
    # Cook wiped Saved/UnrealMCP/port.txt while the bridge kept listening —
    # restorable without a relaunch.
    st = wd.classify(probes(port=None, editor_pids=[42], listener_port=55557,
                            health={"ready": True, "modal_active": False}))
    assert st["state"] == "running_ready"
    assert st["port_file_missing"] is True
    assert st["listener_port"] == 55557


# ---- recovery decisions ---------------------------------------------------

def test_refuses_recovery_during_cook():
    st = wd.classify(probes(cook_pids=[99]))
    decision = wd.plan_recovery(st)
    assert decision["action"] == "refuse"
    assert "cook" in decision["reason"]


def test_plan_rewrite_port_file():
    st = wd.classify(probes(port=None, editor_pids=[42], listener_port=55557,
                            health={"ready": True, "modal_active": False}))
    assert wd.plan_recovery(st)["action"] == "rewrite_port_file"


def test_plan_relaunch_after_crash():
    st = wd.classify(probes(latest_crash=("/tmp/c", "Fatal error", 999.0), now=1000.0))
    d = wd.plan_recovery(st)
    assert d["action"] == "relaunch"
    assert d["kill_remnants"] == []


def test_plan_relaunch_kills_remnants():
    st = wd.classify(probes(editor_pids=[42], remnant_pids=[43, 44],
                            latest_crash=("/tmp/c", "SIGSEGV", 999.0), now=1000.0))
    d = wd.plan_recovery(st)
    assert d["action"] == "relaunch"
    assert set(d["kill_remnants"]) == {42, 43, 44}


def test_plan_none_when_healthy():
    st = wd.classify(probes(port=55557, editor_pids=[42],
                            health={"ready": True, "modal_active": False}))
    assert wd.plan_recovery(st)["action"] == "none"


def test_plan_unwedge_modal():
    st = wd.classify(probes(port=55557, editor_pids=[42],
                            health={"ready": True, "modal_active": True,
                                    "modal_title": "X"}))
    # A hung modal is recovered over the bridge (set_unattended_mode),
    # never by relaunching.
    assert wd.plan_recovery(st)["action"] == "unwedge_modal"


# ---- recovery event shape -------------------------------------------------

def test_recovery_event_shape():
    ev = wd.recovery_event(
        before={"state": "crashed", "crash_summary": "Assertion failed: X",
                "crash_dir": "/tmp/c"},
        action="relaunch", downtime_s=42.5, ok=True)
    assert ev["event"] == "crashed_recovered"
    assert ev["action"] == "relaunch"
    assert ev["ok"] is True
    assert ev["downtime_s"] == pytest.approx(42.5)
    assert "Assertion failed" in ev["crash_summary"]
    # must be JSON-serializable
    json.dumps(ev)
