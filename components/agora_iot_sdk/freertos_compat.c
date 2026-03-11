#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>

/*
 * Agora SDK libahpl.a was built against ESP-IDF 4.x where
 * xTaskCreateRestrictedPinnedToCore was always available.
 * In ESP-IDF 5.x the function is only compiled when
 * portUSING_MPU_WRAPPERS=1, which is not set on ESP32-S3 by default.
 *
 * ESP32-S3 does not enforce hardware MPU restrictions anyway, so
 * delegating to xTaskCreatePinnedToCore is functionally correct.
 *
 * We reproduce the first five fields of TaskParameters_t here because
 * the full struct is guarded by portUSING_MPU_WRAPPERS and would not
 * be visible otherwise.
 */
typedef struct {
    TaskFunction_t  pvTaskCode;
    const char     *pcName;
    uint32_t        usStackDepth;
    void           *pvParameters;
    UBaseType_t     uxPriority;
    /* StackType_t *puxStackBuffer and MemoryRegion_t xRegions[] follow
     * in the real struct but we do not need them here. */
} agora_TaskParameters_t;

BaseType_t xTaskCreateRestrictedPinnedToCore(
    const agora_TaskParameters_t * const pxTaskDefinition,
    TaskHandle_t * const pxCreatedTask,
    const BaseType_t xCoreID)
{
    return xTaskCreatePinnedToCore(
        pxTaskDefinition->pvTaskCode,
        pxTaskDefinition->pcName,
        pxTaskDefinition->usStackDepth,
        pxTaskDefinition->pvParameters,
        pxTaskDefinition->uxPriority,
        pxCreatedTask,
        xCoreID
    );
}
