#!/usr/bin/env python3
"""Overnight validation suite for the UnrealMCP bridge improvements.

Talks raw TCP to the C++ bridge (bypassing the MCP layer, which still runs
the pre-upgrade Python in this session). Creates a scratch Blueprint,
exercises every new/changed command, and cleans up.
"""
import glob
import json
import os
import socket
import struct
import sys
import time


def _find_port_file() -> str:
    """UNREAL_MCP_PROJECT_DIR, else walk up from CWD to the .uproject."""
    env_dir = os.environ.get("UNREAL_MCP_PROJECT_DIR")
    if env_dir:
        return os.path.join(env_dir, "Saved", "UnrealMCP", "port.txt")
    d = os.getcwd()
    while True:
        if glob.glob(os.path.join(d, "*.uproject")):
            return os.path.join(d, "Saved", "UnrealMCP", "port.txt")
        parent = os.path.dirname(d)
        if parent == d:
            return os.path.join("Saved", "UnrealMCP", "port.txt")
        d = parent


PORT_FILE = _find_port_file()
BP_DIR = os.environ.get("UNREAL_MCP_VALIDATE_DIR", "/Game/Dev")
BP_NAME = "BP_MCPValidation"
BP_PATH = f"{BP_DIR}/{BP_NAME}"


def call(cmd, params=None, timeout=120):
    port = int(open(PORT_FILE).read().strip())
    s = socket.create_connection(("127.0.0.1", port), timeout=15)
    s.settimeout(timeout)
    payload = json.dumps({"type": cmd, "params": params or {}}).encode()
    s.sendall(struct.pack(">I", len(payload)) + payload)

    def recvn(n):
        buf = b""
        while len(buf) < n:
            chunk = s.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("socket closed")
            buf += chunk
        return buf

    ln = struct.unpack(">I", recvn(4))[0]
    data = json.loads(recvn(ln).decode())
    s.close()
    return data


def result_of(resp):
    """Unwrap {"status": "success", "result": {...}} envelopes."""
    if resp.get("status") == "error":
        return {"success": False, "error": resp.get("error", "unknown")}
    return resp.get("result", resp)


PASS, FAIL = 0, 0


def check(name, cond, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"PASS  {name}")
    else:
        FAIL += 1
        print(f"FAIL  {name}  ->  {str(detail)[:400]}")


def main():
    # 0. Bridge alive
    r = call("health_check")
    inner = r.get("result", r)
    check("health_check", r.get("status") == "success" and inner.get("status") == "ok", r)

    # Clean leftovers from any earlier run
    call("delete_asset", {"asset_path": f"{BP_PATH}.{BP_NAME}", "force": True})

    # 1. Scratch blueprint
    r = result_of(call("create_blueprint", {"name": BP_NAME, "path": BP_DIR, "parent_class": "Actor"}))
    check("create_blueprint", r.get("success", True) and "error" not in r, r)

    # 2. Event override via alias (BeginPlay -> ReceiveBeginPlay, bOverrideFunction path)
    r = result_of(call("add_event_node", {"blueprint_name": BP_PATH, "event_name": "BeginPlay", "pos_x": 0, "pos_y": 0}))
    check("add_event_node override+alias", r.get("success") and r.get("node_id"), r)
    begin_play_id = r.get("node_id", "")

    # 2b. Idempotency: same event again returns the existing node
    r = result_of(call("add_event_node", {"blueprint_name": BP_PATH, "event_name": "ReceiveBeginPlay"}))
    check("add_event_node dedup (alias vs internal name)", r.get("node_id") == begin_play_id, r)

    # 3. Custom event with parameters
    r = result_of(call("add_custom_event_node", {
        "blueprint_name": BP_PATH, "event_name": "OnFuelChanged",
        "parameters": [
            {"name": "Amount", "type": "float"},
            {"name": "Source", "type": "object:Actor"},
            {"name": "Tags", "type": "string", "is_array": True},
        ],
        "pos_x": 0, "pos_y": 400}))
    check("add_custom_event_node (3 params)", r.get("success") and len(r.get("parameters", [])) == 3, r)

    # 4. Two print nodes
    r = result_of(call("add_blueprint_node", {
        "blueprint_name": BP_PATH, "node_type": "Print",
        "node_params": {"pos_x": 400, "pos_y": 0, "message": "first"}}))
    check("add print node A", r.get("success"), r)
    print_a = r.get("node_id", "")

    r = result_of(call("add_blueprint_node", {
        "blueprint_name": BP_PATH, "node_type": "Print",
        "node_params": {"pos_x": 400, "pos_y": 200, "message": "second"}}))
    check("add print node B", r.get("success"), r)
    print_b = r.get("node_id", "")

    # 5. Connect BeginPlay -> PrintA
    r = result_of(call("connect_nodes", {
        "blueprint_name": BP_PATH,
        "source_node_id": begin_play_id, "source_pin_name": "then",
        "target_node_id": print_a, "target_pin_name": "execute"}))
    check("connect BeginPlay->PrintA", r.get("success"), r)

    # 6. THE BUG FIX: connect the same exec output to PrintB.
    #    Old behaviour: silent double link -> blueprint no longer compiles.
    #    New behaviour: schema breaks the PrintA link and reports it.
    r = result_of(call("connect_nodes", {
        "blueprint_name": BP_PATH,
        "source_node_id": begin_play_id, "source_pin_name": "then",
        "target_node_id": print_b, "target_pin_name": "execute"}))
    broken = r.get("broken_links", [])
    check("exec re-connect auto-breaks old link", r.get("success") and len(broken) == 1, r)
    if broken:
        check("broken link points at PrintA", broken[0].get("to_node", "").lower() == print_a.lower(), broken)

    # 7. Compiles clean after the re-connect
    r = result_of(call("compile_blueprint", {"blueprint_name": BP_PATH}))
    ok = (r.get("compiled") or r.get("success")) and not r.get("errors")
    check("blueprint compiles after re-connect", ok, r)

    # 8. Pin-not-found errors list available pins (error responses now carry
    #    the handler's diagnostic payload under result)
    raw = call("connect_nodes", {
        "blueprint_name": BP_PATH,
        "source_node_id": begin_play_id, "source_pin_name": "nonexistent_pin",
        "target_node_id": print_b, "target_pin_name": "execute"})
    diag = raw.get("result", {})
    check("pin-not-found lists available pins",
          raw.get("status") == "error" and diag.get("available_pins"), raw)

    # 9. Trace forward from BeginPlay
    r = result_of(call("trace_graph", {"blueprint_name": BP_PATH, "event_name": "BeginPlay"}))
    seq = r.get("linear_sequence", [])
    check("trace_graph forward", r.get("success") and len(seq) >= 2, r)
    check("trace reaches PrintB", any(print_b.lower() in s.lower() for s in seq), seq)

    # 10. Disconnect (break all on pin)
    r = result_of(call("disconnect_nodes", {
        "blueprint_name": BP_PATH,
        "source_node_id": begin_play_id, "source_pin_name": "then"}))
    check("disconnect_nodes breaks link", r.get("success") and r.get("links_broken") == 1, r)

    # 11. Delegate Assign: AActor::OnDestroyed on self
    r = result_of(call("add_delegate_node", {
        "blueprint_name": BP_PATH, "delegate_name": "OnDestroyed",
        "owner_class": "self", "mode": "Assign", "pos_x": 0, "pos_y": 800}))
    check("add_delegate_node Assign (bind+event+wire)",
          r.get("success") and r.get("event_connected") and r.get("delegate_node") and r.get("event_node"), r)

    # 11b. Unknown delegate lists available ones
    r = result_of(call("add_delegate_node", {
        "blueprint_name": BP_PATH, "delegate_name": "OnTotallyMadeUp", "owner_class": "self"}))
    check("unknown delegate error lists candidates",
          (not r.get("success")) and "OnDestroyed" in r.get("error", ""), r)

    # 12. Parent call node
    r = result_of(call("add_parent_call_node", {
        "blueprint_name": BP_PATH, "function_name": "ReceiveBeginPlay", "pos_x": 200, "pos_y": 0}))
    check("add_parent_call_node", r.get("success"), r)

    # 13. Compile again with delegates + parent call in graph
    r = result_of(call("compile_blueprint", {"blueprint_name": BP_PATH}))
    check("blueprint compiles with delegate/parent nodes",
          (r.get("compiled") or r.get("success")) and not r.get("errors"), r)

    # 14. save_asset under the unattended guard (the old save-crash path)
    r = result_of(call("save_asset", {"asset_path": f"{BP_PATH}.{BP_NAME}"}))
    check("save_asset under unattended guard", r.get("success"), r)

    # 15. Widget screenshot on a real project widget
    px = result_of(call("execute_python", {"code": (
        "import unreal, json\n"
        "ar = unreal.AssetRegistryHelpers.get_asset_registry()\n"
        "hits = []\n"
        "for a in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/UMGEditor','WidgetBlueprint'), True):\n"
        "    p = str(a.package_name)\n"
        "    if '/Game/' in p and 'WBP_' in p:\n"
        "        hits.append(p)\n"
        "result = sorted(hits)[:25]\n")}))
    paths = px if isinstance(px, list) else px.get("result", px.get("output", []))
    if isinstance(paths, str):
        try:
            paths = json.loads(paths)
        except Exception:
            paths = []
    # prefer a known simple-ish widget if present
    pick = ""
    for cand in paths if isinstance(paths, list) else []:
        if "DifficultySelector" in cand:
            pick = cand
            break
    if not pick and isinstance(paths, list) and paths:
        pick = paths[0]
    check("found widget blueprints to screenshot", bool(pick), paths if not pick else pick)

    if pick:
        r = result_of(call("screenshot_widget", {
            "widget_blueprint_path": pick, "include_layout": True}))
        fp = r.get("file_path", "")
        import os
        size = os.path.getsize(fp) if fp and os.path.exists(fp) else 0
        check("screenshot_widget renders PNG", r.get("success") and size > 1000,
              {"resp": r, "file_size": size})
        check("screenshot_widget layout data", isinstance(r.get("layout"), list) and len(r.get("layout", [])) > 0,
              r.get("layout"))
        if fp:
            print(f"      screenshot: {fp}  ({size} bytes, {r.get('width')}x{r.get('height')}, {len(r.get('layout', []))} widgets in layout)")

    # 16. Cleanup scratch BP
    r = result_of(call("delete_asset", {"asset_path": f"{BP_PATH}.{BP_NAME}", "force": True}))
    check("cleanup scratch blueprint", r.get("success", True), r)

    print(f"\n=== {PASS} passed, {FAIL} failed ===")
    sys.exit(1 if FAIL else 0)


if __name__ == "__main__":
    main()
