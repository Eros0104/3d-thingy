#!/usr/bin/env bash
# Serve the editor over HTTP so fetch('../maps/mansion.json') works without CORS issues.
# Opens the default browser to the editor page.
set -euo pipefail
cd "$(dirname "$0")/.."
PORT="${PORT:-8765}"
echo "editor: http://localhost:${PORT}/editor/"
# Background the server, open the URL, then foreground the server (ctrl-c to stop).
python3 -m http.server "$PORT" &
SERVER_PID=$!
sleep 0.3
if command -v open >/dev/null 2>&1; then
  open "http://localhost:${PORT}/editor/" || true
elif command -v xdg-open >/dev/null 2>&1; then
  xdg-open "http://localhost:${PORT}/editor/" || true
fi
trap "kill $SERVER_PID 2>/dev/null || true" EXIT INT TERM
wait $SERVER_PID
