---
name: ue-mcp-shot-capture
description: Capture stills and multi-angle contact sheets from an Unreal level over MCP — clean camera renders, review grids, and turntables — for marketing captures, look-dev review, QA sweeps, and set-design/VAD deliverables. Use when you need images OF the scene (not debugging screenshots).
---

# Shot capture over MCP

For images a human will look at (marketing, client review, QA), not debug frames.

## Single clean still from a camera
```
capture_from_camera(camera="CC_EngineRoom", max_width=1920)
```
Aims the viewport at the camera actor (its transform + FOV, resolved by label/name/path), captures with `game_view=True` (no gizmos/grid), and restores the viewport. Point it at any actor with a CameraComponent — the authored CineCameras are ideal.

## Contact sheet (the review workhorse)
One labeled grid PNG from many views. Each view is a camera ref or an explicit framing:
```
contact_sheet(views=[
    {"camera": "CC_ControlRoomLobby"},
    {"camera": "CC_EngineRoom"},
    {"camera": "CC_FaceCloseUp"},
    {"location": [0,0,300], "look_at": [0,0,150], "fov": 50, "label": "hero"},
], columns=2, cell_width=640)
```
The response's `cells_row_major` lists the labels in grid order. Great for: "show me all the authored angles", look-dev before/after, QA multi-room sweeps.

## Turntable (orbit an object)
Generate N views orbiting a target and either drop them in a contact sheet or capture individually:
```python
import math
cx, cy, cz, r = 0, 0, 150, 500          # target + radius
views = [{"location": [cx+r*math.cos(t), cy+r*math.sin(t), cz+150],
          "look_at": [cx, cy, cz], "label": f"{int(math.degrees(t))}deg"}
         for t in [i*math.pi/6 for i in range(12)]]   # 12 x 30°
# -> contact_sheet(views=views, columns=4)  or loop capture_from_camera per view
```

## Verify first
Compose from a **ready** editor (`wait_until_ready()`); to capture *runtime* state (BeginPlay effects), `start_pie(simulate=True)` before capturing (see `ue-mcp-verify`).

## Video / Movie Render Queue
For rendered video (turntable clips, shot renders at device quality), the Movie Render Queue is the tool — it needs the **MovieRenderPipeline** plugin enabled and drives a LevelSequence. Not yet wrapped as an MCP command; for now enable the plugin and drive `unreal.MoviePipelineQueueSubsystem` via `execute_python`, or use these stills/contact sheets for image deliverables.

Related: `ue-mcp-verify`, `ue-mcp-look-dev`.
