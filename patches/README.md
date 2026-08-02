# sscma_micro patches

`EPII_CM55M_APP_S/library/sscma_micro` is a submodule pinned at `e830d54`. There is no fork of it,
so the WE2-specific changes live here as patches applied on top of pristine upstream.

Apply with:

    cd EPII_CM55M_APP_S/library/sscma_micro
    git apply ../../../patches/sscma_micro_minimal.patch

## sscma_micro_minimal.patch — 3 files. THIS IS THE ONE YOU NEED.

Verified on hardware 2026-08-02: both YOLO11 and YOLO26 stream and decode at ~10 fps with only
this applied (3x25 s clean runs each, device responsive after).

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

## sscma_micro_full.patch — the full development set, for reference only

Everything that was tried while chasing the YOLO26 freeze. Do NOT apply this; most of it is
obsolete now that the models are int8/100%-NPU, and one hunk is actively wrong:

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
