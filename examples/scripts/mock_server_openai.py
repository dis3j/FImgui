# Minimal stand-in for POST /v1/chat/completions streaming (SSE), used to test Fimgui's OpenAI-compatible chat offline.
import base64, json, random, sys, time
from http.server import BaseHTTPRequestHandler, HTTPServer

OPS_FILE = sys.argv[1]
LOG_FILE = sys.argv[2]
PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 8766

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("content-length", 0)))
        req = json.loads(body)
        auth = self.headers.get("authorization", "")
        messages = []
        for m in req["messages"]:
            content = m["content"]
            if isinstance(content, str):
                messages.append({"role": m["role"], "content": content[:200]})
            else:
                blocks = []
                for b in content:
                    if b["type"] == "image_url":
                        url = b["image_url"]["url"]
                        blocks.append("image:" + url[:30])
                        if url.startswith("data:image/png;base64,"):
                            open(LOG_FILE + ".png", "wb").write(base64.b64decode(url.split(",", 1)[1]))
                    else:
                        blocks.append("text:" + b["text"][:200])
                messages.append({"role": m["role"], "blocks": blocks})
        summary = {
            "path": self.path,
            "authorization": "Bearer ***" if auth else "",
            "model": req.get("model"), "stream": req.get("stream"),
            "reasoning_effort": req.get("reasoning_effort"), "max_tokens": req.get("max_tokens"),
            "messages": messages,
        }
        with open(LOG_FILE, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1, ensure_ascii=False)

        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.end_headers()

        def delta(content=None, reasoning=None, finish=None):
            d = {}
            if content is not None: d["content"] = content
            if reasoning is not None: d["reasoning_content"] = reasoning
            ev = {"id": "chatcmpl-mock", "object": "chat.completion.chunk", "created": 0,
                  "model": req.get("model", "mock"),
                  "choices": [{"index": 0, "delta": d, "finish_reason": finish}]}
            self.wfile.write(f"data: {json.dumps(ev)}\n\n".encode())
            self.wfile.flush()

        delta(reasoning="Planning a dark sidebar layout with four tabs, ")
        delta(reasoning="cards with toggles and sliders.")
        text = "Building a dark game menu with a sidebar, tabs, toggles and sliders.\n"
        text += open(OPS_FILE, encoding="utf-8").read()
        text += "Done - switch tabs in Preview to try it.\n"
        i = 0
        while i < len(text):
            n = random.randint(20, 160)
            delta(content=text[i:i + n])
            i += n
            time.sleep(0.025)
        delta(finish="stop")
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()

HTTPServer(("127.0.0.1", PORT), H).serve_forever()
