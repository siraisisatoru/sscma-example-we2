#!/bin/bash
# Incremental app rebuild + image gen. Reuses the prebuilt cmsis_nn/tflm archives
# and skips `make clean`, so it takes ~1 min instead of ~10.
# Usage: ./quick_rebuild.sh [EXTRA_MAKE_VARS...]
set -e
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ensure the sscma_micro submodule (pristine upstream, no fork) has the
# WE2-required patch applied. Idempotent: skips if already applied.
# See patches/README.md for what this patch does and why.
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

docker run --rm -v "$ROOT:/workspace" sscma-yolo26-build:26.05 bash -c "
  cd /workspace/EPII_CM55M_APP_S &&
  make CMSIS_NN_LIB_FORCE_PREBUILT=y INFERENCE_FORCE_PREBUILT=y $* -j\$(nproc) &&
  cd /workspace/we2_image_gen_local &&
  cp /workspace/EPII_CM55M_APP_S/obj_epii_evb_icv30_bdv10/gnu_epii_evb_WLCSP65/EPII_CM55M_gnu_epii_evb_WLCSP65_s.elf input_case1_secboot/ &&
  ./we2_local_image_gen project_case1_blp_wlcsp.json | tail -5
"
# Fail loudly if image-gen silently kept a stale output.img (the fragile
# memory-descriptor check in we2_local_image_gen can do exactly that).
IMG="$ROOT/we2_image_gen_local/output_case1_sec_wlcsp/output.img"
if [ ! -f "$IMG" ] || [ $(( $(date +%s) - $(stat -f %m "$IMG") )) -gt 120 ]; then
  echo "ERROR: output.img is missing or stale (image gen did not rewrite it)"; exit 1
fi
echo "OK: $(ls -l "$IMG")"
