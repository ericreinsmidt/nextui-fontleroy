#!/bin/sh
set -eu

PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
APP_ID="fontleroy"

STATE_DIR="${USERDATA_PATH}/${APP_ID}"
FONTS_DIR="${STATE_DIR}/fonts"

BIN="${PAK_DIR}/bin/fontleroy"

mkdir -p "${STATE_DIR}" "${FONTS_DIR}"

if [ -x "${BIN}" ]; then
  export FONTLEROY_PAK_DIR="${PAK_DIR}"
  export FONTLEROY_STATE_DIR="${STATE_DIR}"
  export FONTLEROY_FONTS_DIR="${FONTS_DIR}"

  cd "${PAK_DIR}"
  exec "${BIN}"
else
  echo "Executable not found: ${BIN}"
  exit 0
fi
