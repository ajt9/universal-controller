#!/bin/bash

# Determine repository location.
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

PORT=8000

# Verify HTML file exists.
if [ ! -f "$PROJECT_DIR/CMAPIWebServer.html" ]; then
    echo "Error: HTML file not found!"
    exit 1
fi

echo "Starting Universal Controller..."

# Host the repository root.
exec python3 -m http.server \
    "$PORT" \
    --bind 0.0.0.0 \
    --directory "$PROJECT_DIR"

# html file accessible at http://10.12.194.1:8000/UniversalControllerWebServer.html
