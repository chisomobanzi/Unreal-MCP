"""Pure decision logic for the animation workflow tools.

Kept editor-free so the loud-failure gates are unit-testable: the entire
reason batch_retarget exists is that UE's retarget APIs fail silently
(empty ops stack, headless auto-align no-op) and produce ref-pose bakes
that LOOK like success at the asset level.
"""
from __future__ import annotations

# Chains a humanoid source rig legitimately cannot drive (e.g. UE mannequins
# have no eye bones; gaze is owned by the eye-tracking component anyway).
DEFAULT_ALLOWED_UNMAPPED = frozenset({"leftEye", "rightEye"})

# Below this much end-bone travel (cm) across the sampled window, a baked
# clip is considered static — the signature of a silent ref-pose bake.
DEFAULT_MOTION_THRESHOLD_CM = 0.5


def plan_preview_times(length: float, times: list[float] | None,
                       default_count: int = 4) -> list[float]:
    """Timestamps to screenshot for a clip of `length` seconds.

    Explicit times are clamped into [0, length]; when omitted, spread
    `default_count` samples across the interior of the clip (never the
    exact endpoints, which are the most ref-pose-like frames).
    """
    if length <= 0.0:
        return [0.0]
    if times:
        return [min(max(0.0, t), length) for t in times]
    n = max(1, default_count)
    return [round(length * (i + 1) / (n + 1), 3) for i in range(n)]


def evaluate_retarget_output(coverage: dict[str, str | None],
                             per_clip_motion: dict[str, float],
                             *,
                             motion_threshold_cm: float = DEFAULT_MOTION_THRESHOLD_CM,
                             allowed_unmapped: frozenset[str] = DEFAULT_ALLOWED_UNMAPPED,
                             ) -> dict:
    """Judge a batch-retarget run. Returns {ok, errors, warnings, unmapped, dead_clips}.

    coverage: target chain name -> source chain name (None/'' = unmapped).
    per_clip_motion: baked clip name -> sampled end-bone travel in cm
        (empty dict = motion probe unavailable; the gate is skipped).
    """
    errors: list[str] = []
    warnings: list[str] = []

    unmapped = sorted(t for t, s in coverage.items() if not s or s == "None")
    mapped_count = len(coverage) - len(unmapped)

    if coverage and mapped_count == 0:
        errors.append(
            "No chains mapped from source to target — the retargeter has an "
            "empty or unmapped chain mapping (did the ops stack exist before "
            "auto_map_chains ran?). Every bake would be a ref-pose dud.")

    unexpected = [u for u in unmapped if u not in allowed_unmapped]
    if unexpected and mapped_count > 0:
        warnings.append(
            f"Unmapped target chains (will hold ref pose): {', '.join(unexpected)}")

    dead = sorted(c for c, d in per_clip_motion.items()
                  if d < motion_threshold_cm)
    if per_clip_motion and len(dead) == len(per_clip_motion):
        errors.append(
            f"All {len(dead)} baked clips are static (<{motion_threshold_cm}cm "
            "end-bone travel) — silent ref-pose bake. Outputs are junk.")
    elif dead:
        warnings.append(
            f"Static baked clips (<{motion_threshold_cm}cm end-bone travel): "
            f"{', '.join(dead)} — verify these are intentionally static.")

    return {
        "ok": not errors,
        "errors": errors,
        "warnings": warnings,
        "unmapped": unmapped,
        "dead_clips": dead,
    }
