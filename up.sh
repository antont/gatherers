#!/usr/bin/env bash
set -euo pipefail

BUCKET="gs://an.org/gatherers/"

WASM_FILE=$(ls dist/*.wasm 2>/dev/null | head -1)
WASM_BR="${WASM_FILE}.br"

if [ -z "$WASM_FILE" ]; then
    echo "ERROR: No .wasm file in dist/. Run ./build.sh first."
    exit 1
fi

if [ ! -f "$WASM_BR" ]; then
    echo "No brotli-compressed .wasm found. Compressing now..."
    brotli -9 -f "$WASM_FILE"
fi

WASM_BASENAME=$(basename "$WASM_FILE")

echo "==> Uploading non-WASM assets..."
gsutil -m -h "Cache-Control:public, max-age=600" \
    cp -r $(ls dist/* | grep -v '\.wasm') "$BUCKET"

echo "==> Uploading brotli-compressed WASM as $WASM_BASENAME..."
gsutil -h "Content-Encoding:br" \
       -h "Content-Type:application/wasm" \
       -h "Cache-Control:public, max-age=600" \
    cp "$WASM_BR" "${BUCKET}${WASM_BASENAME}"

echo "==> Deploy complete: https://an.org/gatherers/"
