#include "pressure_sensor.h"

#include <sdkconfig.h>

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

#include <esp_log.h>

#define TAG "PressureSensor"

bool PressureSensor::Initialize() {
    return true;
}

bool PressureSensor::IsPressed() {
    return false;
}

int PressureSensor::GetPressureValue() {
    return 0;
}

void PressureSensor::SetThreshold(int threshold) {
    threshold_ = threshold;
}

#endif // CONFIG_ENABLE_DOLL_INTERACTION

