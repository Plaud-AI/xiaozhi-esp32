#include "touch_sensor.h"

#include <sdkconfig.h>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>

#define TAG "TouchSensor"

bool TouchSensor::Initialize() {
    return true;
}

TouchPosition TouchSensor::GetTouchPosition() {
    return TouchPosition::kTouchNone;
}

bool TouchSensor::IsTouched() {
    return false;
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

