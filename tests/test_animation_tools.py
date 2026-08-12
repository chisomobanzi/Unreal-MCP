"""Pure-logic guards for the animation workflow tools (no editor required).

plan_preview_times: timestamp planning for preview_animation.
evaluate_retarget_output: the loud-failure gate for batch_retarget — the
whole point of the tool is that silent ref-pose bakes become hard errors.
"""
import pytest

from unrealmcp._animtools import evaluate_retarget_output, plan_preview_times


# ---------------------------------------------------------------------------
# plan_preview_times
# ---------------------------------------------------------------------------

def test_default_times_spread_interior():
    times = plan_preview_times(10.0, None)
    assert len(times) == 4
    assert times == sorted(times)
    assert times[0] > 0.0 and times[-1] < 10.0


def test_explicit_times_clamped_to_length():
    times = plan_preview_times(5.0, [0.0, 2.5, 99.0])
    assert times[0] == 0.0
    assert times[1] == 2.5
    assert 0.0 < times[2] <= 5.0


def test_degenerate_length_yields_time_zero():
    assert plan_preview_times(0.0, None) == [0.0]


# ---------------------------------------------------------------------------
# evaluate_retarget_output
# ---------------------------------------------------------------------------

def _coverage(mapped, unmapped):
    cov = {c: f"src_{c}" for c in mapped}
    cov.update({c: None for c in unmapped})
    return cov


def test_zero_mapped_chains_is_error():
    rep = evaluate_retarget_output(_coverage([], ["spine", "head"]), {})
    assert rep["ok"] is False
    assert any("no chains mapped" in e.lower() for e in rep["errors"])


def test_allowed_unmapped_chains_do_not_warn():
    rep = evaluate_retarget_output(
        _coverage(["spine", "head"], ["leftEye", "rightEye"]),
        {"AS_Clip": 12.0},
    )
    assert rep["ok"] is True
    assert rep["errors"] == []
    assert rep["warnings"] == []


def test_unexpected_unmapped_chains_warn_but_pass():
    rep = evaluate_retarget_output(
        _coverage(["spine"], ["leftHand"]),
        {"AS_Clip": 12.0},
    )
    assert rep["ok"] is True
    assert any("leftHand" in w for w in rep["warnings"])


def test_all_clips_static_is_refpose_error():
    rep = evaluate_retarget_output(
        _coverage(["spine", "head"], []),
        {"AS_A": 0.0, "AS_B": 0.02},
    )
    assert rep["ok"] is False
    assert any("ref-pose" in e.lower() for e in rep["errors"])
    assert set(rep["dead_clips"]) == {"AS_A", "AS_B"}


def test_some_clips_static_warns_and_names_them():
    rep = evaluate_retarget_output(
        _coverage(["spine"], []),
        {"AS_A": 9.0, "AS_B": 0.0},
    )
    assert rep["ok"] is True
    assert rep["dead_clips"] == ["AS_B"]
    assert any("AS_B" in w for w in rep["warnings"])


def test_no_motion_data_skips_motion_gate():
    rep = evaluate_retarget_output(_coverage(["spine"], []), {})
    assert rep["ok"] is True
    assert rep["dead_clips"] == []
