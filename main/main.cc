#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

    // 初始化BLE WiFi配网服务
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "初始化 BLE WiFi 配网服务");
    ESP_LOGI(TAG, "========================================");
    
    auto& provisioner = BLEWiFiProvisioner::GetInstance();
    
    // 设置配网成功回调
    provisioner.SetProvisionSuccessCallback([](const std::string& ssid, const std::string& password) {
        ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   ✅ WiFi配网成功！                    ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
        ESP_LOGI(TAG, "SSID: %s", ssid.c_str());
        ESP_LOGI(TAG, "设备将在2秒后重启...");
    });
    
    // 设置配网失败回调
    provisioner.SetProvisionFailureCallback([](const std::string& error_message) {
        ESP_LOGE(TAG, "╔════════════════════════════════════════╗");
        ESP_LOGE(TAG, "║   ❌ WiFi配网失败                      ║");
        ESP_LOGE(TAG, "╚════════════════════════════════════════╝");
        ESP_LOGE(TAG, "错误: %s", error_message.c_str());
        ESP_LOGE(TAG, "请在手机App中重试");
    });
    
    // 初始化并启动BLE WiFi配网
    if (provisioner.Initialize("ESP32-PLAUD")) {
        provisioner.Start();
        ESP_LOGI(TAG, "✓ BLE WiFi配网服务已启动");
    } else {
        ESP_LOGE(TAG, "❌ BLE WiFi配网服务初始化失败");
    }

    ESP_LOGI(TAG, "========================================");

    // Launch the application
    auto& app = Application::GetInstance();
    app.Start();
}
