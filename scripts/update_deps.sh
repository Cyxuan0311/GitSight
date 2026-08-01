#!/bin/sh
# Update vendored dependencies (cJSON + miniz) from upstream with mirror fallback
set -e

CJSON_VERSION="1.7.18"
CJSON_DIR="$(dirname "$0")/../third_party/cJSON"

MINIZ_VERSION="3.0.2"
MINIZ_DIR="$(dirname "$0")/../third_party/miniz"

MIRROR="${DEPS_MIRROR:-https://mirror.ghproxy.com/}"

download() {
    local url="$1"
    local out="$2"
    echo "Downloading $out from $url ..."
    curl -fL "$url" -o "$out" && return 0
    return 1
}

update_file() {
    local url="$1" out="$2"
    download "${url}" "${out}" || \
    download "${MIRROR}${url}" "${out}" || {
        echo "ERROR: Failed to download $(basename "$out")"
        echo "Set DEPS_MIRROR env var to a custom mirror URL."
        exit 1
    }
}

# --- cJSON ---
CJSON_URL="https://raw.githubusercontent.com/DaveGamble/cJSON/v${CJSON_VERSION}"
update_file "${CJSON_URL}/cJSON.h" "${CJSON_DIR}/cJSON.h"
update_file "${CJSON_URL}/cJSON.c" "${CJSON_DIR}/cJSON.c"
echo "cJSON v${CJSON_VERSION} updated."

# --- miniz ---
MINIZ_URL="https://raw.githubusercontent.com/richgel999/miniz/${MINIZ_VERSION}"
for f in miniz.h miniz.c miniz_common.h miniz_export.h \
         miniz_tdef.h miniz_tdef.c miniz_tinfl.h miniz_tinfl.c \
         miniz_zip.h miniz_zip.c; do
    update_file "${MINIZ_URL}/${f}" "${MINIZ_DIR}/${f}"
done
echo "miniz v${MINIZ_VERSION} updated."

echo "All dependencies up to date."
