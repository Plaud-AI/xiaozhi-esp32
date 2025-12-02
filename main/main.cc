#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_bt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "system_info.h"

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

    // ====== 内存优化：提前释放 Classic BT 内存 ======
    // BLE 控制器将在需要时（WiFi 配网或常驻模式）延迟初始化
    // ⚠️ 注意：不在此处初始化 BLE 控制器，以为 WiFi 留出足够的内部 RAM
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔧 内存优化：释放 Classic BT 内存");
    ESP_LOGI(TAG, "========================================");
    
    // 释放 Classic BT 内存（节省 30-40KB 内部 RAM）
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 已释放 Classic BT 内存（约 30-40KB）");
    } else {
        ESP_LOGW(TAG, "⚠️  释放 Classic BT 内存失败: %d (可能已释放)", ret);
    }
    
    // 注意：BLE 控制器延迟初始化策略
    // - WiFi 初始化需要大量内部 RAM（静态缓冲区等）
    // - 提前初始化 BLE 会导致 WiFi 初始化失败（ESP_ERR_NO_MEM）
    // - BLE 控制器将在以下时机初始化：
    //   1. WiFi 配网模式：由 BLEWiFiProvisioner 按需初始化
    //   2. WiFi 成功后：在 WiFi 连接成功后启动 BLE 常驻模式
    ESP_LOGI(TAG, "   - BLE 控制器将延迟初始化（WiFi 成功后）");
    ESP_LOGI(TAG, "   - WiFi 将优先获得内部 RAM");
    ESP_LOGI(TAG, "========================================");

    // Launch the application
    // WiFi 将在 Application::Start() 中初始化
    // BLE 将在需要时（配网或 WiFi 成功后）按需初始化
    auto& app = Application::GetInstance();
    app.Start();
}
