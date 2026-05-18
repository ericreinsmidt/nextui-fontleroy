#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PLATFORM="tg5040"
PAK_DIR="$PROJECT_DIR/ports/$PLATFORM/pak"
DIST_DIR="$PROJECT_DIR/dist"
PAK_JSON="$PROJECT_DIR/pak.json"

APP_NAME="$(grep -E '"name"' "$PAK_JSON" | head -n1 | cut -d'"' -f4)"
RELEASE_FILENAME="$(grep -E '"release_filename"' "$PAK_JSON" | head -n1 | cut -d'"' -f4)"

echo ""
echo "=== Packaging ${APP_NAME} for ${PLATFORM} ==="
echo ""

# Validate
if [ -z "$APP_NAME" ] || [ -z "$RELEASE_FILENAME" ]; then
    echo "ERROR: Failed to read name or release_filename from pak.json"
    exit 1
fi

if [ ! -f "$PAK_DIR/bin/fontleroy" ]; then
    echo "ERROR: Binary not found at $PAK_DIR/bin/fontleroy — run 'make build' first"
    exit 1
fi

# Create clean zip via temp dir (excludes .DS_Store)
mkdir -p "$DIST_DIR"
TMP_DIR="$DIST_DIR/tmp-package"
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR"

rsync -a --exclude='.DS_Store' "$PAK_DIR/" "$TMP_DIR/"

# Validate
if [ ! -f "$TMP_DIR/launch.sh" ]; then
    echo "ERROR: Missing launch.sh in pak directory"
    rm -rf "$TMP_DIR"
    exit 1
fi

OUTPUT_ZIP="$DIST_DIR/$RELEASE_FILENAME"
rm -f "$OUTPUT_ZIP"
cd "$TMP_DIR"
zip -r "$OUTPUT_ZIP" ./*
cd "$PROJECT_DIR"
rm -rf "$TMP_DIR"

echo ""
echo "=== Package complete ==="
echo "Output: dist/$RELEASE_FILENAME"
echo ""
