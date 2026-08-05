#ifndef _MA_CONFIG_BOARD_H_
#define _MA_CONFIG_BOARD_H_

#ifndef MA_PLATFORM_HIMAX
    #error "Include with explicit platform"
#else
    #ifdef __cplusplus
extern "C" {
    #endif

    #include <WE2_device.h>
    #include <hx_drv_gpio.h>
    #include <hx_drv_scu.h>
    #include <hx_drv_scu_export.h>

    #ifdef TRUSTZONE_SEC
        #if (__ARM_FEATURE_CMSE & 1) == 0
            #error "Need ARMv8-M security extensions"
        #elif (__ARM_FEATURE_CMSE & 2) == 0
            #error "Need compile with '--cmse'"
        #endif

        #include <arm_cmse.h>

        #ifdef NSC
            #include <veneer_table.h>
        #endif
        #ifndef TRUSTZONE_SEC_ONLY
            #include <secure_port_macros.h>
        #endif
    #endif
    #ifdef __cplusplus
}
    #endif
#endif

#ifdef MA_BOARD_GROVE_VISION_AI_V2
    #include "boards/ma_board_grove_vision_ai_v2.h"
#elif defined(MA_BOARD_WATCHER)
    #include "boards/ma_board_watcher.h"
#else
    #error "Missing board configuration"
#endif


#define MA_USE_ENGINE_TFLITE               1
#define MA_ENGINE_TFLITE_TENSOE_ARENA_SIZE (1100 * 1024)
#define MA_USE_ENGINE_TENSOR_INDEX         1
// Arena is placed in CM55M_S_EL_ALLOC via a dedicated linker section
// (ma_tensor_arena.cpp) instead of pvPortMalloc — see that file for why.
#define MA_USE_STATIC_TENSOR_ARENA         1

#define MA_TFLITE_OP_SOFTMAX               1
#define MA_TFLITE_OP_PADV2                 1
#define MA_TFLITE_OP_TRANSPOSE             1
#define MA_TFLITE_OP_ETHOS_U               1
#define MA_TFLITE_OP_DEQUANTIZE            1
#define MA_TFLITE_OP_QUANTIZE              1
#define MA_TFLITE_OP_FULLY_CONNECTED       1
#define MA_TFLITE_OP_GATHER                1
#define MA_TFLITE_OP_RESHAPE               1
#define MA_TFLITE_OP_MAX_POOL_2D           1
#define MA_TFLITE_OP_MUL                   1
#define MA_TFLITE_OP_BROADCAST_TO          1
#define MA_TFLITE_OP_CONV_2D               1
#define MA_TFLITE_OP_DEPTHWISE_CONV_2D     1
#define MA_TFLITE_OP_ADD                   1
#define MA_TFLITE_OP_PAD                   1
#define MA_TFLITE_OP_CONCATENATION         1
#define MA_TFLITE_OP_LOGISTIC              1
#define MA_TFLITE_OP_SPLIT                 1
#define MA_TFLITE_OP_SLICE                 1
#define MA_TFLITE_OP_STRIDED_SLICE         1
// YOLO26n's C2PSA backbone attention blocks lower to BATCH_MATMUL (unrelated
// to the detection head / DFL); needed for any YOLO26 model.
#define MA_TFLITE_OP_BATCH_MATMUL          1

#define MA_CONFIG_OSAL_FREERTOS_USE_PII    1

#define MA_INVOKE_ENABLE_RUN_HOOK          1

#define MA_SENSOR_ENCODE_USE_STATIC_BUFFER 1
#define MA_SENSOR_ENCODE_STATIC_BUFFER_ADDR (0x36000000 + (200 * 1024))
#define MA_SENSOR_ENCODE_STATIC_BUFFER_SIZE (0x36060000 - MA_SENSOR_ENCODE_STATIC_BUFFER_ADDR)


#define MA_FILESYSTEM_LITTLEFS             1
#define MA_STORAGE_LFS_USE_FLASHBD         1
#define MA_OSAL_RTOS_EXTERN_PII            1
#define MA_HAS_NATTIVE_WIFI_SUPPORT        0
#define MA_HAS_NATTIVE_MQTT_SUPPORT        0

#define MA_OSAL_FREERTOS                   1
#define MA_SEVER_AT_EXECUTOR_STACK_SIZE    (20 * 1024)
#define MA_SEVER_AT_EXECUTOR_TASK_PRIO     2

// UART_1 (PB6/PB7) TX ring — the XIAO/ESP32 companion header. The 4 KB default
// in ma_transport_serial.cpp only covers AT responses and results-only INVOKE
// events; an ESP32 node running AT+INVOKE=-1,0 pushes a base64 JPEG per frame,
// which is larger than that ring, so every frame would take send()'s ring-full
// path. Set here rather than passed as APPL_DEFINES on the make command line:
// a command-line APPL_DEFINES assignment discards every `APPL_DEFINES +=` in
// the makefiles (verified on the build container's GNU Make 4.3), silently
// dropping -DSSCMA, -DIC_PACKAGE_WLCSP65 and the rest.
#define MA_TRANSPORT_SERIAL_TX_RING_SIZE   (24 * 1024)

#define MA_DEBUG_LEVEL                     1

// #define MA_CONFIG_BOARD_I2C_SLAVE          1

#if MA_OSAL_FREERTOS
#include "freertos_compat.h"
#endif

#endif  // _MA_CONFIG_BOARD_H_
