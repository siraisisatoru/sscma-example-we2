# Face Recognition Integration — Postmortem and Recommended Path

This note records an attempt to add on-device face **recognition** (detection +
128-D embedding + identity match) to the Grove Vision AI Module V2, why the first
approach was abandoned, and the path that works. It is a companion to
`face_embedding_sscma_integration_analysis.md` (the up-front plan for the approach
that was tried) and `face_recognition_data_path.md` (the data-path reference).

## Goal

A live face-recognition demo streaming, per frame, a JPEG preview plus each face's
bounding box, 5 landmarks and a 128-D embedding, with identity matching against an
enrolled gallery. Target: >= 1 fps, low resolution acceptable.

## Approach A (abandoned): port the recognition pipeline into the `sscma` app

The first approach kept the `sscma` AT-server application (FreeRTOS + the
`sscma_micro` abstraction layer + the AT/JSON console transport) and grafted the
face pipeline from the official `tflm_face_embedding` example onto it. The
motivation was to reuse the existing AT-command host tooling.

This compiled and streamed, but never became reliable. Three independent,
architecture-specific defects were found, each traced on hardware:

### 1. Lost UART TX-done completion (stream stalls under load)

The `sscma` console transport streams over UART using a DMA path. On the WE2 the
UART0 TX-done completion is a PL080 DMAC3 channel interrupt that **shares one NVIC
line** with UART0 RX and the UART1 channels. The shared handler services one
channel per invocation and every peripheral IRQ runs at the same NVIC priority, so
under load (or during a camera/NPU interrupt burst) two DMAC3 completions coalesce
onto one already-set pending bit and a TX-done is dropped. The SoC never re-pends
for a status bit that is already high, so the stream stalls until the next
unrelated DMAC3 event (e.g. an incoming byte) happens to re-enter the handler.

Switching the transport from DMA to blocking PIO `uart_write` (which polls the
line-status register and cannot lose a completion — the same mechanism the stock
`tflm_face_embedding` uses) removed this failure mode. But it exposed the next one.

### 2. Pipeline hang (whole inference loop stops)

With the transport fixed, the stream still died after tens of seconds. A per-frame
heartbeat (frame counter + free-heap, emitted on a raw UART path that survives a
wedged transport) showed the heap healthy and stable at the moment of death, and
the heartbeat stopping at the same instant as the stream. The conclusion was that
the **inference/camera pipeline itself hangs** — most likely an Ethos-U55 or
camera-datapath completion interrupt lost to the same NVIC-priority/coalescing
issue as (1), just on a different peripheral. A hardware watchdog fed from the
frame loop recovered it automatically, but that only converts a permanent hang into
a reboot every 10-30 s — acceptable as a safety net, not as a product.

### 3. Face detection returned nothing (SCRFD score identically 0)

Even when detection did run, SCRFD reported a maximum score of exactly 0 on frames
where the identical model, run on a host PC over the same captured image, detected
a face at ~0.4. The SCRFD post-processing code is byte-identical between this app
and the stock example, and the model input was verified to contain valid,
correctly-quantized image data — so the fault was specific to this app's
integration, the prime suspect being its tensor-arena sharing hack (it points the
face models' arenas into the AT-server's otherwise-unused `EL_ALLOC` region to fit
the SRAM budget, which the stock example does not do).

### Why it was abandoned

All three defects stem from the surrounding stack, not from the face pipeline:
the shared-IRQ DMA transport, the FreeRTOS task/handshake timing, and the memory
overlays needed to make everything co-exist. Each fix exposed the next problem.
The post-processing and JSON output were already identical to the stock example, so
the integration was adding fragility without adding capability.

## Approach B (works): use the stock `tflm_face_embedding` app

The official `tflm_face_embedding` example already does the whole job — SCRFD
detection + landmark alignment + MobileFaceNet 128-D embedding — on the stock
bare-metal event loop, over **polled** UART, with **no** arena sharing. Verified on
hardware:

- Detection: face found on ~52/60 frames, confidence 1.0.
- Output: the same `{"type":1,"name":"FACE_RESULT","code":0,"data":{image,
  resolution,faces:[{bbox,confidence,landmarks,embedding,embedding_format}]}}`
  JSON the host tooling already parses.
- No transport stall and no pipeline hang over sustained streaming.

Identity matching works once the gallery is enrolled from the **board's own
embeddings** rather than from host-side photo embeddings: the on-device (Vela)
MobileFaceNet and a host (non-Vela) MobileFaceNet produce embeddings that are not
directly comparable (measured same-person similarity ~0.2-0.3 across pipelines vs
~0.9 board-to-board). Enrolling from a few high-confidence live frames and
averaging the L2-normalized mean gives reliable live recognition (~0.9 cosine,
0.6 threshold).

## Key lessons

- **Prefer the stock example's architecture.** The face pipeline is the same in
  both; the sscma integration only added a fragile transport, task model and memory
  overlay. Reusing the working app and adding a thin interface is far less work than
  hardening the port.
- **Polled UART TX is immune to the shared-DMAC3 lost-completion class of bug.**
  If a DMA console transport must be used, note that UART0 TX, UART0 RX and the
  UART1 channels share DMAC3 IRQ 69 at a single NVIC priority; completions can
  coalesce and be dropped under load.
- **Enroll from the same pipeline you recognize with.** Board (Vela) and host
  (non-Vela) embeddings of the same model are not interchangeable.
- **Instrument before theorizing.** A raw-UART per-frame heartbeat (frame counter +
  free heap) that survives a wedged transport was what distinguished "transport
  stalled" from "pipeline hung" from "heap exhausted" — three failures that look
  identical from the host (the stream simply stops).

## If AT-command compatibility is needed

The stock app free-runs (streams from boot) and has no AT command parser, while the
AT host tooling issues `AT+INVOKE` to start streaming and a few queries
(`AT+ID?`, `AT+MODEL?`). The two frame formats are otherwise the same envelope
(`\r{"type":..,"name":..,"code":..,"data":..}\n`), so the host reader already parses
both. `AT+INVOKE` is not functionally required — a free-running stream is read
directly — it only matters because some host viewers exit if the `AT+INVOKE`
handshake does not return. The minimal bridge is therefore to have the stock app
**acknowledge** `AT+INVOKE` (and answer `AT+ID?`/`AT+MODEL?`) with a `type:0,
code:0` response while continuing to free-run; no start/stop or reconfiguration
semantics are needed for a dedicated single-purpose device.
