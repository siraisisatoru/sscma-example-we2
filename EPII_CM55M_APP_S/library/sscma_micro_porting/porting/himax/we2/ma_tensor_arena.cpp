// Static tensor arena for the TFLite Micro engine.
//
// The FreeRTOS global `operator new`/`new[]` (ma_device.cpp) routes through
// pvPortMalloc(), backed by the .ucHeap region (CM55M_S_SRAM, 336 KB — see
// grove.ld). The engine's tensor arena is ~1.1 MB, far larger than that heap,
// so a heap-backed allocation silently returns nullptr (pvPortMalloc has no
// assert on OOM here) and AllocateTensors() corrupts state downstream,
// HALTING regardless of model contents.
//
// CM55M_S_EL_ALLOC (1112 KB, grove.ld) is reserved for exactly this kind of
// large buffer and is otherwise unused by this app (.elHeap is empty), so we
// place the arena there directly via a dedicated linker section instead of
// going through the small FreeRTOS heap.
//
// ---------------------------------------------------------------------------
// Overflow/underflow canaries
//
// This arena has dangerous neighbours on BOTH sides, and neither the NPU nor
// TFLM reports an overrun:
//
//   0x34000000  CM55M_S_SRAM      FreeRTOS heap (.ucHeap, 336 KB)
//   0x34054000  CM55M_S_EL_ALLOC  <- this arena, 1100 KB, ends 0x34167000
//   0x3416A000  SRAM_1_TAIL       YUV422 camera framebuffer, 600 KB
//                                 (see drivers/camera/drv_shared_cfg.h)
//
// Only 12 KB separates the end of this arena from the YUV422 framebuffer the
// sensor DMA is actively filling; the FreeRTOS heap sits immediately below.
// An overrun corrupts live camera data, an underrun corrupts the heap — and
// either would present as an intermittent hang inside the capture step rather
// than as any kind of allocation error.
//
// These canaries make both cases detectable and attributable, for 64 bytes and
// one comparison per inference.
#include <cstddef>
#include <cstdint>

extern "C" {

__attribute__((section(".bss.elHeap"), aligned(16))) static uint32_t s_canary_lo[8];
__attribute__((section(".bss.elHeap"), aligned(16))) static uint8_t  s_tensor_arena[1100 * 1024];
__attribute__((section(".bss.elHeap"), aligned(16))) static uint32_t s_canary_hi[8];

uint8_t* _ma_static_tensor_arena = s_tensor_arena;

#define MA_ARENA_CANARY 0xC0DEFEEDu

void ma_tensor_arena_arm_canaries(void) {
    for (size_t i = 0; i < 8; ++i) {
        s_canary_lo[i] = MA_ARENA_CANARY;
        s_canary_hi[i] = MA_ARENA_CANARY;
    }
}

// 0 = intact, 1 = low (heap-side) clobbered, 2 = high (camera-side) clobbered,
// 3 = both.
int ma_tensor_arena_check_canaries(void) {
    int rc = 0;
    for (size_t i = 0; i < 8; ++i) {
        if (s_canary_lo[i] != MA_ARENA_CANARY) {
            rc |= 1;
            break;
        }
    }
    for (size_t i = 0; i < 8; ++i) {
        if (s_canary_hi[i] != MA_ARENA_CANARY) {
            rc |= 2;
            break;
        }
    }
    return rc;
}

uintptr_t ma_tensor_arena_base(void) { return (uintptr_t)s_tensor_arena; }
size_t    ma_tensor_arena_size(void) { return sizeof(s_tensor_arena); }
}
