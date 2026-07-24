#pragma once
/* FreeRTOS 10.5.1 compatibility shim
 * sscma_micro uses uxQueueGetQueueLength and uxQueueSpacesAvailableFromISR
 * which were added in a newer FreeRTOS. Provide equivalents here.
 */
#include "FreeRTOS.h"
#include "queue.h"

static inline UBaseType_t uxQueueGetQueueLength(QueueHandle_t x) {
    return uxQueueMessagesWaiting(x) + uxQueueSpacesAvailable(x);
}

static inline UBaseType_t uxQueueSpacesAvailableFromISR(QueueHandle_t x) {
    return uxQueueSpacesAvailable(x);
}
