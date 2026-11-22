import base64, http.client, json, subprocess, sys, time

PORT = int(sys.argv[1])
OPS = sys.argv[2]
OUT = sys.argv[3]
BRIDGE = sys.argv[4]

session = None
def rpc(method, params=None, id=1, notify=False):
    global session
    msg = {"jsonrpc": "2.0", "method": method}
    if params is not None: msg["params"] = params
    if not notify: msg["id"] = id
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=60)
    h = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if session: h["Mcp-Session-Id"] = session
    c.request("POST", "/mcp", json.dumps(msg), h)
    r = c.getresponse()
    body = r.read()
    session = r.getheader("Mcp-Session-Id") or session
    return r.status, (json.loads(body) if body else None)

for _ in range(60):
    try:
        st, init = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test-client", "title": "MCP test client", "version": "1"}})
        break
    except OSError:
        time.sleep(0.25)
print("initialize", st, init["result"]["protocolVersion"], init["result"]["serverInfo"], "session:", bool(session))
print("initialized ->", rpc("notifications/initialized", notify=True)[0])
st, tl = rpc("tools/list", {}, 2)
print("tools:", [t["name"] for t in tl["result"]["tools"]])

def call(name, args, id):
    st, r = rpc("tools/call", {"name": name, "arguments": args}, id)
    return r["result"]

g = call("get_design_guide", {}, 3)
print("guide chars:", len(g["content"][0]["text"]), "isError:", g["isError"])

ops = [json.loads(l) for l in open(OPS, encoding="utf-8") if l.strip()]
t0 = time.time()
r = call("apply_operations", {"operations": ops}, 4)
print("apply_operations:", json.loads(r["content"][0]["text"])["applied"], "applied in", round(time.time() - t0, 2), "s; isError:", r["isError"])

r = call("apply_operations", {"operations": [{"op": "update", "id": "card_title", "props": {"text": "Hello from MCP"}}, {"op": "update", "id": "nope", "props": {}}]}, 5)
res = json.loads(r["content"][0]["text"])
print("partial batch:", res["applied"], "ok,", res["failed"], "failed:", res["results"][1].get("error"))

r = call("render_image", {}, 6)
img = [c for c in r["content"] if c["type"] == "image"][0]
open(OUT, "wb").write(base64.b64decode(img["data"]))
print("render_image:", img["mimeType"], [c["text"] for c in r["content"] if c["type"] == "text"])

r = call("get_code", {"node_id": "btn_play", "format": "imgui"}, 7)
print("get_code imgui first line:", r["content"][0]["text"].splitlines()[0])
r = call("get_document", {"node_id": "card"}, 8)
print("get_document card keys:", sorted(json.loads(r["content"][0]["text"]).keys())[:8])
r = call("preview", {"mode": "play", "node_id": "card"}, 9)
print("preview play:", r["content"][0]["text"])
r = call("document_file", {"action": "new"}, 10)
print("new with unsaved changes ->", r["isError"], r["content"][0]["text"][:60])
r = call("no_such_tool", {}, 11)
print("unknown tool isError:", r["isError"])
st, e = rpc("bogus/method", {}, 12)
print("unknown method:", e["error"]["code"])
print("GET ->", http.client.HTTPConnection("127.0.0.1", PORT).request("GET", "/mcp") or "sent")

# stdio bridge
p = subprocess.Popen([BRIDGE, "--port", str(PORT)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
lines = [{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "bridge-test"}}},
         {"jsonrpc": "2.0", "method": "notifications/initialized"},
         {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "apply_operations", "arguments": {"operations": [{"op": "update", "id": "card_title", "props": {"text": "Hello via stdio bridge"}}]}}}]
out, err = p.communicate("\n".join(json.dumps(l) for l in lines).encode() + b"\n", timeout=30)
resp = [json.loads(l) for l in out.decode().splitlines() if l.strip()]
print("bridge responses:", len(resp), "ids", [r.get("id") for r in resp], "->", json.loads(resp[1]["result"]["content"][0]["text"])["applied"], "applied")
print("bridge stderr:", err.decode().strip().splitlines()[0])
