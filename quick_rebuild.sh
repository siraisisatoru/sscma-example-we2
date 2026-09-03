#!/bin/bash
# Incremental app rebuild + image gen. Reuses the prebuilt cmsis_nn/tflm archives
# so a build takes ~1-3 min instead of ~10.
#
# Usage:
#   ./quick_rebuild.sh                              # rebuild the default app (makefile APP_TYPE)
#   ./quick_rebuild.sh --app tflm_face_embedding    # rebuild a specific app (face recognition)
#   ./quick_rebuild.sh --app <name> [EXTRA_MAKE_VARS...]
#
# --app overrides APP_TYPE without editing the makefile. Switching to a different
# app than the last build triggers a `make clean` automatically (FORCE_PREBUILT
# keeps that fast). The sscma_micro patch is applied ONLY for apps that compile
# the sscma_micro AT layer (e.g. `sscma`); other examples build straight from the
# tree. See docs/face_recognition_integration_postmortem.md for the face path.
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- parse args: --app <name> is consumed here; everything else is passed to make
APP=""
MAKE_ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --app)   APP="$2"; shift 2;;
    --app=*) APP="${1#*=}"; shift;;
    *)       MAKE_ARGS+=("$1"); shift;;
  esac
done

DEFAULT_APP=$(grep -E '^APP_TYPE' "$ROOT/EPII_CM55M_APP_S/makefile" | head -1 | sed 's/.*=[[:space:]]*//;s/[[:space:]]*$//')
APP="${APP:-$DEFAULT_APP}"
APP_MK="$ROOT/EPII_CM55M_APP_S/app/scenario_app/$APP/$APP.mk"
if [ ! -f "$APP_MK" ]; then
  echo "ERROR: unknown app '$APP' (no $APP_MK)"; exit 1
fi

# Ensure the sscma_micro submodule has the WE2-required patch applied, but only
# for apps that actually compile it (idempotent; skips if already applied).
# See patches/README.md.
if grep -q 'sscma_micro' "$APP_MK"; then
  SSCMA_MICRO_DIR="$ROOT/EPII_CM55M_APP_S/library/sscma_micro"
  PATCH="$ROOT/patches/sscma_micro_full.patch"
  if ! git -C "$SSCMA_MICRO_DIR" apply --check "$PATCH" 2>/dev/null; then
    if git -C "$SSCMA_MICRO_DIR" apply --reverse --check "$PATCH" 2>/dev/null; then
      echo "sscma_micro: patch already applied, skipping"
    else
      echo "ERROR: $PATCH does not apply cleanly to sscma_micro (dirty submodule or upstream drift?)"
      exit 1
    fi
  else
    echo "sscma_micro: applying $PATCH"
    git -C "$SSCMA_MICRO_DIR" apply "$PATCH"
  fi
fi

# Clean when the app differs from the last build (switching apps needs a clean
# tree, or make relinks the wrong scenario app). Marker lives beside the obj tree
# so `make clean` (which only removes the obj tree) does not wipe it.
MARKER="$ROOT/EPII_CM55M_APP_S/.built_app"
LAST=$(cat "$MARKER" 2>/dev/null || echo "$DEFAULT_APP")
CLEAN=""
if [ "$LAST" != "$APP" ]; then
  echo "app changed ($LAST -> $APP): forcing a clean build"
  CLEAN="make APP_TYPE=$APP clean &&"
fi

docker run --rm -v "$ROOT:/workspace" sscma-yolo26-build:26.05 bash -c "
  cd /workspace/EPII_CM55M_APP_S &&
  $CLEAN
  make APP_TYPE=$APP CMSIS_NN_LIB_FORCE_PREBUILT=y INFERENCE_FORCE_PREBUILT=y ${MAKE_ARGS[*]} -j\$(nproc) &&
  cd /workspace/we2_image_gen_local &&
  cp /workspace/EPII_CM55M_APP_S/obj_epii_evb_icv30_bdv10/gnu_epii_evb_WLCSP65/EPII_CM55M_gnu_epii_evb_WLCSP65_s.elf input_case1_secboot/ &&
  ./we2_local_image_gen project_case1_blp_wlcsp.json | tail -5
"
echo "$APP" > "$MARKER"

# Fail loudly if image-gen silently kept a stale output.img (the fragile
# memory-descriptor check in we2_local_image_gen can do exactly that).
IMG="$ROOT/we2_image_gen_local/output_case1_sec_wlcsp/output.img"
if [ ! -f "$IMG" ] || [ $(( $(date +%s) - $(stat -f %m "$IMG") )) -gt 180 ]; then
  echo "ERROR: output.img is missing or stale (image gen did not rewrite it)"; exit 1
fi
echo "OK ($APP): $(ls -l "$IMG")"
