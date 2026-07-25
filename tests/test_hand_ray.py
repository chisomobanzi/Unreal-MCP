"""hand_ray_select sub-widget targeting: the ray must be aimable at a NAMED
child widget, not just the target actor's widget centre (editor-less)."""
import pytest

from unrealmcp import _player


@pytest.fixture
def bridge(monkeypatch):
    """Record every hand_ray_click call; reply with a scripted queue."""
    calls = []
    replies = []

    def fake_send(cmd, params=None):
        calls.append((cmd, params or {}))
        return replies.pop(0) if replies else {"status": "success", "result": {}}

    monkeypatch.setattr("unrealmcp._tcp_bridge._tcp_send_raw", fake_send)
    return calls, replies


def ok_reply(**extra):
    result = {"success": True, "clicked": True, "hovered": "Panel"}
    result.update(extra)
    return {"status": "success", "result": result}


def test_named_sub_widget_is_forwarded_to_the_bridge(bridge):
    calls, replies = bridge
    replies.append(ok_reply(hit_inside_widget=True, aimed_widget="Btn_Confirm"))

    _player.hand_ray_select("BP_MissionPanel", widget="Btn_Confirm")

    _, params = calls[0]
    assert params.get("widget") == "Btn_Confirm", (
        "the sub-widget name never reached the bridge — the ray still aims at "
        "the actor's widget centre")


def test_widget_component_disambiguator_is_forwarded(bridge):
    calls, replies = bridge
    replies.append(ok_reply(hit_inside_widget=True))

    _player.hand_ray_select("BP_Hub", widget="Btn_Next",
                            widget_component="LeftPanelWidget")

    _, params = calls[0]
    assert params.get("widget_component") == "LeftPanelWidget"


def test_click_landing_outside_the_named_widget_is_not_ok(bridge):
    """Hitting the panel but missing the button is a MISS, not a pass — this is
    exactly the failure the old centre-aim produced silently."""
    calls, replies = bridge
    replies.append(ok_reply(hit_inside_widget=False))
    replies.append(ok_reply(hit_inside_widget=False))  # occluder-bypass retry

    res = _player.hand_ray_select("BP_MissionPanel", widget="Btn_Confirm")

    assert res["ok"] is False


def test_click_inside_the_named_widget_is_ok(bridge):
    _, replies = bridge
    replies.append(ok_reply(hit_inside_widget=True))

    res = _player.hand_ray_select("BP_MissionPanel", widget="Btn_Confirm")

    assert res["ok"] is True


def test_no_named_widget_keeps_the_old_centre_aim_contract(bridge):
    """Callers that don't name a widget must behave exactly as before: no
    stray params, and hit_inside_widget is irrelevant."""
    calls, replies = bridge
    replies.append(ok_reply())

    res = _player.hand_ray_select("BP_SkillPod_Fluency")

    _, params = calls[0]
    assert "widget" not in params and "widget_component" not in params
    assert res["ok"] is True


def test_occluder_retry_keeps_the_sub_widget_aim(bridge):
    """The bypass retry must re-aim at the same named widget, not fall back to
    the centre — otherwise the retry silently clicks the wrong thing."""
    calls, replies = bridge
    replies.append(ok_reply(clicked=False, hovered=""))
    replies.append(ok_reply(hit_inside_widget=True))

    res = _player.hand_ray_select("BP_MissionPanel", widget="Btn_Confirm")

    assert len(calls) == 2
    assert calls[1][1].get("widget") == "Btn_Confirm"
    assert res["ok"] is True
    assert res["needed_occluder_bypass"] is True
