#!/usr/bin/env python3
# Serve a Draconic web export from THIS folder. Python 3 stdlib only.
#
#   python3 serve.py [port]        (default 8000)  ->  http://localhost:8000/Engine.Player.html
#
# A plain static server is not quite enough for a wasm app:
#   - .wasm needs the application/wasm MIME type (streaming compile),
#   - COOP/COEP headers make the page cross-origin isolated (required whenever the build
#     uses SharedArrayBuffer; harmless otherwise),
#   - Cache-Control: no-store, because browsers cache the large .wasm/.pak HARD and a
#     plain reload often serves a stale build.
import http.server
import socketserver
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
        ".pak": "application/octet-stream",
        ".dpak": "application/octet-stream",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


class Server(socketserver.TCPServer):
    allow_reuse_address = True


with Server(("", PORT), Handler) as httpd:
    print(f"serving on http://localhost:{PORT}/ (Ctrl+C to stop)")
    print(f"open   http://localhost:{PORT}/Engine.Player.html")
    httpd.serve_forever()
