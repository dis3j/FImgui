# Minimal stand-in for POST /v1/messages streaming (SSE), used to test Fimgui's AI pipeline offline.
import json, random, sys, time
from http.server import BaseHTTPRequestHandler, HTTPServer

OPS_FILE = sys.argv[1]
LOG_FILE = sys.argv[2]
PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 8765

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("content-length", 0)))
        req = json.loads(body)
        # Record what the client sent (without huge image payloads).
        summary = {
            "path": self.path,
            "headers": {k: (v if k.lower() != "x-api-key" else "***") for k, v in self.headers.items()},
            "model": req.get("model"), "max_tokens": req.get("max_tokens"), "stream": req.get("stream"),
            "thinking": req.get("thinking"), "output_config": req.get("output_config"), "fallbacks": req.get("fallbacks"),
            "system_cache": req["system"][0].get("cache_control"), "system_len": len(req["system"][0]["text"]),
            "messages": [{"role": m["role"], "blocks": [b["type"] + (":" + b["source"]["media_type"] + f" {len(b['source']['data'])}b64" if b["type"] == "image" else ":" + b["text"][:120]) for b in m["content"]]} for m in req["messages"]],
        }
        import base64
        for m in req["messages"]:
            for b in m["content"]:
                if b["type"] == "image":
                    open(LOG_FILE + ".png", "wb").write(base64.b64decode(b["source"]["data"]))
        with open(LOG_FILE, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1, ensure_ascii=False)

        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.end_headers()

        def ev(name, data):
            self.wfile.write(f"event: {name}\ndata: {json.dumps(data)}\n\n".encode())
            self.wfile.flush()

        ev("message_start", {"type": "message_start", "message": {"id": "msg_mock", "type": "message", "role": "assistant", "content": []}})
        ev("content_block_start", {"type": "content_block_start", "index": 0, "content_block": {"type": "thinking", "thinking": ""}})
        for part in ["Planning a dark sidebar layout with four tabs, ", "cards with toggles and sliders."]:
            ev("content_block_delta", {"type": "content_block_delta", "index": 0, "delta": {"type": "thinking_delta", "thinking": part}})
            time.sleep(0.1)
        ev("content_block_stop", {"type": "content_block_stop", "index": 0})
        ev("content_block_start", {"type": "content_block_start", "index": 1, "content_block": {"type": "text", "text": ""}})
        text = "Building a dark game menu with a sidebar, tabs, toggles and sliders.\n"
        text += open(OPS_FILE, encoding="utf-8").read()
        text += "Done - switch tabs in Preview to try it.\n"
        i = 0
        while i < len(text):
            n = random.randint(20, 160)
            ev("content_block_delta", {"type": "content_block_delta", "index": 1, "delta": {"type": "text_delta", "text": text[i:i + n]}})
            if random.random() < 0.15:
                self.wfile.write(b"event: ping\ndata: {\"type\": \"ping\"}\n\n")
            i += n
            time.sleep(0.025)
        ev("content_block_stop", {"type": "content_block_stop", "index": 1})
        ev("message_delta", {"type": "message_delta", "delta": {"stop_reason": "end_turn"}, "usage": {"output_tokens": 1234}})
        ev("message_stop", {"type": "message_stop"})

HTTPServer(("127.0.0.1", PORT), H).serve_forever()
