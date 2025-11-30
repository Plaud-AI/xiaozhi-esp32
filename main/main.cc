#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_bt.h>
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
    // 关键：必须在控制器初始化前释放 Classic BT 内存
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔧 初始化蓝牙控制器（WiFi/BLE共存）");
    ESP_LOGI(TAG, "========================================");
    
    // 步骤1：在控制器初始化前释放 Classic BT 内存
    // 这样控制器只会为 BLE 分配内存，节省 30-40KB
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 已释放 Classic BT 内存（约 30-40KB）");
    } else {
        ESP_LOGW(TAG, "⚠️  释放 Classic BT 内存失败: %d (可能已释放)", ret);
    }
    
    // 步骤2：初始化 BLE 控制器（只为 BLE 分配内存）
    ret = nimble_port_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 蓝牙控制器初始化成功（BLE-only 模式）");
        ESP_LOGI(TAG, "   - WiFi 和 BLE 现在可以共存");
        ESP_LOGI(TAG, "   - 内存优化：仅分配 BLE 所需内存");
        ESP_LOGI(TAG, "   - BLE 服务将在需要时启动");
    } else {
        ESP_LOGE(TAG, "❌ 蓝牙控制器初始化失败: %d", ret);
        ESP_LOGW(TAG, "   - 将继续启动，但 BLE 功能将不可用");
    }
    ESP_LOGI(TAG, "========================================");

    // Launch the application
    auto& app = Application::GetInstance();
    app.Start();
    
    // ⚠️ CRITICAL: Double-check and force disable BLE controller if WiFi is connected
    // This is a failsafe to prevent coexistence crashes (StoreProhibited in timer_insert)
    // if WifiBoard::StartNetwork failed to disable it for some reason.
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔒 Failsafe: Ensuring BLE Controller is Disabled");
    ESP_LOGI(TAG, "========================================");
    
    // Wait a bit to ensure WiFi connection logic has settled
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    #ifdef CONFIG_BT_ENABLED
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        ESP_LOGW(TAG, "⚠️ BLE Controller found ENABLED! Disabling now to prevent crash...");
        // First stop the provisioner/NimBLE stack if running
        BLEWiFiProvisioner::GetInstance().Stop();
        // Then disable the controller hardware
        ret = esp_bt_controller_disable();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "✅ BLE Controller FORCIBLY disabled");
        } else {
            ESP_LOGE(TAG, "❌ Failed to disable BLE Controller: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGI(TAG, "✅ BLE Controller is already disabled (Safe)");
    }
    #endif
    ESP_LOGI(TAG, "========================================");
}
