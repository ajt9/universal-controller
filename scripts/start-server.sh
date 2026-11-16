#!/bin/bash

# Determine repository location.
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PORT=8000

# Verify HTML file exists.
if [ ! -f "$PROJECT_DIR/UniversalControllerWebServer.html" ]; then
    echo "Error: HTML file not found!"
    exit 1
fi

echo "Starting Universal Controller..."

# Host the repository root.
exec python3 -m http.server \
    "$PORT" \
    --bind 0.0.0.0 \
    --directory "$PROJECT_DIR"
