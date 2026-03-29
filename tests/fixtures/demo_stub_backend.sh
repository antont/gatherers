#!/usr/bin/env bash
set -euo pipefail

log_dir="${GATHERERS_DEMO_LOG_DIR:?GATHERERS_DEMO_LOG_DIR is required}"
addr="${GATHERERS_BACKEND_RUST_ADDR:?GATHERERS_BACKEND_RUST_ADDR is required}"

mkdir -p "${log_dir}"
printf '%s\n' "$$" > "${log_dir}/backend.pid"

host="${addr%:*}"
port="${addr##*:}"

cleanup() {
  if [[ -n "${server_pid:-}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
    kill "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

python3 - "${host}" "${port}" <<'PY' &
import http.server
import socketserver
import sys

host = sys.argv[1]
port = int(sys.argv[2])

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/healthz", "/dashboard.css"):
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"ok")
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        pass

with socketserver.TCPServer((host, port), Handler) as httpd:
    httpd.serve_forever()
PY
server_pid=$!
wait "${server_pid}"
