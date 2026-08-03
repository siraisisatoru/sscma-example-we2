# sscma_micro patches

`EPII_CM55M_APP_S/library/sscma_micro` is a submodule pinned at `e830d54`. There is no fork of it,
so the WE2-specific changes live here as patches applied on top of pristine upstream.

`sscma_micro_full.patch` also carries the UART1/ESP32 companion-link fixes to
`EPII_CM55M_APP_S/library/sscma_micro_porting/porting/himax/we2/ma_transport_serial.cpp` (see
§"UART1/ESP32 companion-link fixes" below and `esp_node/README.md` §10.1) — that file is **not**
part of the submodule, so this one patch file spans two different apply-roots (the submodule root
for the sscma_micro hunks, the repo root for the `ma_transport_serial.cpp` hunk). Because of that
it is applied from the **repo root**, not from inside the submodule:

    git apply patches/sscma_micro_full.patch

`quick_rebuild.sh` applies it automatically (idempotent), so normally you do not apply anything by
hand.

## sscma_micro_yolo11_quant_swap — the YOLO11 duplicate-box fix (in the full patch)

Found 2026-08-03. `Yolo11::postProcessI8()` dequantized the class logit with the BOX tensor's
scale/zero-point and the box with the CLS tensor's — the two `outputs_[i*2]` / `outputs_[i*2+1]`
indices were swapped. `Yolo26::postProcessI8()` is correct upstream (it uses named `box_idx_` /
`cls_idx_`), so this is YOLO11-only.

It stayed latent because YOLO11 used to be exported by the kris-himax fork with `no_post=False`,
which baked the DFL decode into the model and emitted ONE decoded-box tensor — so this function
never ran. Unifying YOLO11 onto the YOLO26 export path (6 raw heads + forced int8 boundary) binds
the `Yolo11` decoder and runs `postProcessI8()` for the first time.

Measured on the real deer export (`yolo11_int8.tflite`, nc=1, 192px), decoding identical int8
tensors both ways:

    image               swapped: raw ->nms  maxconf  medW      fixed: raw ->nms  maxconf  medW
    -110_jpg.rf.63c16a       39     8   0.9999   59.7                8     1   0.7609  128.0
    -286_jpg.rf.b4fe319     101    22   1.0000   38.6                3     1   0.6903   35.4
    -416_jpg.rf.2ad4dbd     160    35   1.0000   38.5               12     2   0.7443   52.7

Two independent effects, both from the same swap: the cls zero-point (127 vs 32) inflates every
logit by ~+10 so background cells clear the gate at sigmoid~1.0, and the box scale (0.056 vs 0.109,
0.51x) flattens the DFL softmax so duplicates come out wrong-sized and fall under the NMS IoU
threshold. Hence stacks of ~100%-confident overlapping boxes.

Note the fixed peak confidence is ~0.76, not ~0.99, and marginal frames can drop to zero
detections at a 0.5 threshold. That is the honest accuracy of this int8 export (cls scale 0.056 =
coarse logit granularity), not a residual bug. Tune with `AT+TSCORE=` or more `--cal-images`.

## sscma_micro_minimal.patch — 3 files. SUPERSEDED, do not use.

Verified on hardware 2026-08-02: both YOLO11 and YOLO26 stream and decode at ~10 fps with only
this applied (3x25 s clean runs each, device responsive after). That result did NOT hold on retest,
and `quick_rebuild.sh` was moved to the full patch in a381ab5. Kept for reference; the three fixes
below are all also present in the full patch.

**`sscma/core/utils/ma_nms.h`** — `compute_iou()` treated `ma_bbox_t.x`/`.y` as the box's TOP-LEFT
corner. Every single producer in this codebase (yolov5/yolov8/yolo11/yolo26/fomo/nvidia_det, both
`_hailo` variants) writes the box CENTER there instead (`x_min + w/2`). Treating a center as a
corner extends the "box" IoU sees an extra half-width right and half-height down of where it
actually is — since two overlapping detections of the same object rarely share identical w/h, this
does not cancel out between them and systematically UNDERESTIMATES true IoU (hand-verified: true
IoU ~0.76 computed as ~0.59). Below the 0.45 default NMS threshold, that let genuine duplicate
detections survive as separate boxes — most visible on YOLO11's dense one-to-many head, where
several adjacent grid cells/scales confidently detect the same object and are supposed to collapse
into one. This is a correctness bug affecting EVERY detector in the SDK, not specific to YOLO11/26.

Measured impact on a real deer scene: peak boxes/frame for the same object dropped from 6 to 4
(median stayed ~2 either way — the remaining overlap is very likely legitimate: distinct
detections whose true IoU genuinely sits below 0.45, which this fix does not and should not force
together). If residual overlap still looks wrong, lower `threshold_nms_` via `AT+TIOU=<pct>` (e.g.
30 instead of the 45 default) — that's a tuning knob, not a further bug fix, and it trades away
some ability to keep genuinely close-but-distinct objects separate.

**`sscma/server/at/callback/refactor_required.hpp`** — the only genuinely load-bearing change.
Upstream's `setAlgorithmInput()` switch lists neither `MA_MODEL_TYPE_YOLO11` nor
`MA_MODEL_TYPE_YOLO26`, so both fall through to `default: return MA_ENOTSUP` and inference never
runs. `serializeAlgorithmOutput()` has YOLO11 but not YOLO26, so YOLO26 events omit the `boxes`
key entirely. Measured without the patch: `AT+INVOKE` is accepted (code 0), exactly ONE event
frame arrives with error code 8 (`MA_ENOTSUP`) and no `boxes` key, then the stream stops.

This one cannot be worked around from outside the submodule: `refactor_required.hpp` is included
as `#include "refactor_required.hpp"` (quoted, same directory) from `invoke.hpp` and `trigger.hpp`,
and a quoted include resolves relative to the including file first, so include-path shadowing
cannot shadow it. It is a genuine upstream gap and worth filing.

**`sscma/core/cv/ma_cv.cpp`** — adds the `MA_PIXEL_FORMAT_RGB888_PLANAR` (NCHW) destination to
`yuv422p_to_rgb()` and to the `convert()` dispatch. Needed because every litert-torch export is
NCHW `(1,3,H,W)`, so `Detector` sets `img_.format` to planar and upstream `convert()` would
otherwise return unsupported.

NOTE: every conversion function in `ma_cv.cpp` is `MA_ATTR_WEAK`, so this half *could* instead be a
strong override compiled into `sscma_micro_porting/` with no submodule edit at all. Untested —
both objects land in the same `libsscma_micro_porting.a` and weak-vs-strong resolution inside one
archive is member-order dependent, so verify with `nm`/the map file before relying on it.

## UART1/ESP32 companion-link fixes (in the full patch)

Fixes 3 defects in `Serial` (UART_1, the XIAO/ESP32 header pins) needed to stream image payloads
to a companion MCU — see `esp_node/README.md` for the full companion-link design (pin mapping,
bandwidth budget, bring-up). `Console` (UART_0, the CH343 USB bridge) was hardened for large
payloads during the YOLO26 freeze investigation; `Serial` was deliberately left alone at the time
("nothing streams image payloads over it" — no longer true once an ESP32 is on the header):

1. **Non-atomic TX-DMA claim.** `send()` did a bare `if (!_tx_busy) { _tx_busy = true; ... }` from
   task context while the DMA ISR clears `_tx_busy` whenever it finds the ring empty. Lose that
   race and bytes sit in the ring with no DMA in flight — the stream stops dead, permanently. Fixed
   with an interrupt-disabled `_tx_kick()`, mirroring `Console`'s existing fix.
2. **`flush()` was a no-op.** The invoke loop calls `_transport->flush()` after every frame
   specifically to pace itself to the link so the ring never fills. Fixed to actually wait for
   drain (bounded at 200k yields, as `Console` does).
3. **4 KB TX ring**, smaller than a single image frame, so every frame went through the
   ring-full path — exactly the path defect 1 lived in. The ring size becomes
   `MA_TRANSPORT_SERIAL_TX_RING_SIZE` (still defaulting to 4 KB — raise it only when the ESP32
   link carries JPEG payloads, e.g. `APPL_DEFINES+=-DMA_TRANSPORT_SERIAL_TX_RING_SIZE=24576`; 24 KB
   is a reasonable start for 320×240, budgeted against the same FreeRTOS heap the AT/JSON event
   path shares).

Together, defects 1–3 mean boxes-only streaming (`AT+INVOKE=-1,1`) worked fine even before this
fix; image streaming (`AT+INVOKE=-1,0`) would wedge after a handful of frames without it.
`send()` is also now bounded (200k stalled yields) so a stuck DMA drops the frame instead of
wedging the Executor task forever with no output at all — same reasoning as `Console::send`.

## sscma_micro_full.patch — THIS IS THE ONE THAT GETS APPLIED

`quick_rebuild.sh` applies it. It is the full development set from chasing the YOLO26 freeze, the
YOLO11 quant-swap fix above, and the UART1/ESP32 companion-link fixes above. Several of the
submodule hunks are obsolete or questionable rather than needed — annotated below so the next
person knows which is which.

**Regenerating it is a two-step manual process** — it is NOT a plain `git diff` of one location,
because it spans the submodule and the main tree:

    cd EPII_CM55M_APP_S/library/sscma_micro
    git diff | perl -pe '
      s{^diff --git a/(\S+) b/(\S+)$}{diff --git a/EPII_CM55M_APP_S/library/sscma_micro/$1 b/EPII_CM55M_APP_S/library/sscma_micro/$2};
      s{^--- a/(\S+)$}{--- a/EPII_CM55M_APP_S/library/sscma_micro/$1};
      s{^\+\+\+ b/(\S+)$}{+++ b/EPII_CM55M_APP_S/library/sscma_micro/$1};
    ' > ../../../patches/sscma_micro_full.patch
    cd ../../..
    git diff EPII_CM55M_APP_S/library/sscma_micro_porting/porting/himax/we2/ma_transport_serial.cpp \
      >> patches/sscma_micro_full.patch

(plain `sed` is deliberately not used here: BSD sed, the default on macOS, doesn't support `\|`
alternation in basic regexes the way GNU sed does, so a `sed`-based one-liner that looks correct
can silently no-op on macOS — verified the hard way. `perl -pe` behaves the same on both.)

The `perl` step rewrites the submodule's naturally-relative paths (`a/sscma/...`) to be relative to
the repo root (`a/EPII_CM55M_APP_S/library/sscma_micro/sscma/...`), so the whole combined file can
be applied with one `git apply` from the repo root. **Do not** regenerate with a plain
`cd sscma_micro && git diff > ...` one-liner — that silently drops the UART1 hunk with no error,
since it only captures the submodule half. `quick_rebuild.sh` reverse-checks the whole file to
decide whether it's already applied, so whatever you regenerate must apply/reverse-apply cleanly
as one unit from the repo root — verify with:

    git apply --check patches/sscma_micro_full.patch   # or --reverse --check if already applied

- `ma_model_detector.cpp` — in-place F32 expansion. Dead: input is now `type=2` (S8) and upstream's
  existing `input_.data.u8[i] -= 128` path handles it.
- `ma_model_yolo26.cpp` — 16 hunks, all diagnostics except a `sigmoid_score` refactor
  (functionally identical) and an `isnan()` guard inside `postProcessF32()`, which int8 never reaches.
- `ma_engine_tflite.cpp` — `AddBatchMatMul()`. Unnecessary (Vela reports `CPU operators = 0`) and
  **broken**: the `OpsCount` enum in `ma_engine_tflite.h` has no `AddBatchMatMul` slot, so the
  `MicroMutableOpResolver` array is one entry too small and `AddBuiltin()` returns an error the
  constructor ignores — one op silently fails to register.
- `ma_codec_json.cpp` — adaptive `PrintBuffered` prebuffer + `reserve()` (robustness under heap
  pressure) and `m_data = nullptr` in `reset()` (a genuine latent dangling-pointer fix). Optional;
  upstream behaviour is fine now that the unused UART1 ring buffer was reclaimed in the parent repo.
- `invoke.hpp` — `_transport->flush()` after each frame. Dropping it roughly DOUBLED throughput
  (5 fps -> 10 fps) with no loss of stability, so leaving it out is the better default.
