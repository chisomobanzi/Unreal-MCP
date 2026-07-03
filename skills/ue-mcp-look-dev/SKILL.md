---
name: ue-mcp-look-dev
description: Iterate on materials and lighting in Unreal over MCP — build/edit a material graph, apply it, verify, and tune parameters, plus the sharp UMG/material gotchas. Use when authoring or adjusting materials, sky/lighting, or Widget Blueprints via the MCP bridge.
---

# Material / lighting look-dev over MCP

## The loop
1. **Build the graph in one shot** — `build_material_graph(material_path, nodes, connections)`. It's atomic and safe-rebuilds (clears + rewires, external refs intact). A whole procedural material can be a single **`Custom` HLSL node** → `EmissiveColor`: inside HLSL you can read view direction via `Parameters.CameraVector` and time via `View.GameTime`, so no input pins are needed. Set `shading_model="unlit"` for skies/emissive.
2. **Check it compiled** — `get_material_errors(material_path)` (`has_errors:false`). A graph that "built" can still fail to compile.
3. **Apply** — for a level actor, set the mesh component's material (via `execute_python` `set_material`) or `apply_material_to_actor`. For a Blueprint, `apply_material_to_blueprint`.
4. **Verify** — see `ue-mcp-verify`. If the actor's material is swapped at BeginPlay by a BP, you must `start_pie(simulate=True)` to see the runtime result; the editor shows the authored material.
5. **Tune** — expose scalars/vectors as parameters (`ScalarParameter`/`VectorParameter` nodes) and adjust a Material **Instance** with `set_material_instance_parameter` — no recompile, fast iteration.

## Sharp gotchas (learned the hard way)
- **Equirect textures on a sky dome via a generic sample-and-rotate material render BLURRY** (atan2-UV derivative blowup → lowest mip). Prefer a **procedural** Custom-HLSL sky, or a material that samples at an explicit mip. Confirmed: the shared `M_SpaceSky` equirect path smears; procedural is crisp.
- **Imported textures default to `TEXTUREGROUP_World` + streaming**, which mips a skybox down to mush. On import pass `texture_options={"lod_group":"TEXTUREGROUP_Skybox","never_stream":true}` (or set them post-import + save).
- **Auto-exposure (histogram) will wash out a darker scene.** Pin it on the PostProcessVolume (override min=max AutoExposure brightness) before judging a dusk/night look.
- After a python `set_material`, the viewport can show a **stale render proxy** — nudge the camera to force a redraw before screenshotting.

## UMG
- After `rename_widget`, DisplayLabel is cleared automatically (fixed in the plugin) — no manual follow-up.
- Reparenting a WBP to a C++ base that BindWidget-declares a designer widget the graph reads: the C++ property must be `BlueprintReadOnly` or the BP won't compile.
- Compile a WBP before `screenshot_widget` (off-screen render needs a compiled tree).

Related: `ue-mcp-verify`, `ue-mcp-shot-capture`.
