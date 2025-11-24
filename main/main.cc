#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nimble/nimble_port.h>

#include "application.h"
#include "system_info.h"
#include "ble_wifi_provisioner.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ====== 提前初始化蓝牙控制器（WiFi/BLE 共存要求）======
    // ESP32-S3 要求蓝牙控制器必须在 WiFi 之前初始化
    // 这样可以让 WiFi 和 BLE 正确共存
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔧 提前初始化蓝牙控制器（WiFi/BLE共存）");
    ESP_LOGI(TAG, "========================================");
    
    ret = nimble_port_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 蓝牙控制器初始化成功");
        ESP_LOGI(TAG, "   - WiFi 和 BLE 现在可以共存");
        ESP_LOGI(TAG, "   - BLE 服务将在需要时启动");
    } else {
        ESP_LOGE(TAG, "❌ 蓝牙控制器初始化失败: %d", ret);
        ESP_LOGW(TAG, "   - 将继续启动，但 BLE 功能将不可用");
    }
    ESP_LOGI(TAG, "========================================");

    // Launch the application
    auto& app = Application::GetInstance();
    app.Start();
}
