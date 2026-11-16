#!/bin/bash

# Find the repository directory automatically.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

WEB_DIR="$PROJECT_DIR/web"
PORT=8000

# Verify the HTML application exists.
if [ ! -f "$WEB_DIR/UniversalControllerWebServer.html" ]; then
    echo "Error: HTML file not found!"
    exit 1
fi

echo "Starting Universal Controller Web Server..."

# Start the web server.
exec python3 -m http.server \
    "$PORT" \
    --bind 0.0.0.0 \
    --directory "$WEB_DIR"
