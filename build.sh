#!/usr/bin/env bash
set -euo pipefail

echo "==> Building release WASM with trunk..."
trunk build --release

echo "==> Build complete. Contents of dist/:"
ls -lh dist/

WASM_FILE=$(ls dist/*.wasm 2>/dev/null | head -1)
if [ -z "$WASM_FILE" ]; then
    echo "ERROR: No .wasm file found in dist/"
    exit 1
fi

RAW_SIZE=$(wc -c < "$WASM_FILE" | tr -d ' ')
echo "==> Raw WASM size: $(echo "$RAW_SIZE" | awk '{printf "%.1f MiB", $1/1048576}') ($RAW_SIZE bytes)"

echo "==> Compressing with brotli..."
brotli -9 -f "$WASM_FILE"
BR_SIZE=$(wc -c < "${WASM_FILE}.br" | tr -d ' ')
echo "==> Brotli size:   $(echo "$BR_SIZE" | awk '{printf "%.1f MiB", $1/1048576}') ($BR_SIZE bytes, $(echo "$RAW_SIZE $BR_SIZE" | awk '{printf "%.0f%% smaller", (1-$2/$1)*100}'))"

echo ""
echo "==> Done. Run ./up.sh to deploy to GCS."
