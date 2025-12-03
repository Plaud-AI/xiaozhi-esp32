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

    // ====== 内存优化：释放 Classic BT 内存 + 提前初始化 BLE 控制器 ======
    // ⚠️ 重要：BLE 控制器必须在 WiFi 之前初始化，否则内存分配会失败
    // 原因：BLE 控制器需要固定的内部 SRAM，WiFi 启动后会占用这部分内存
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🔧 初始化 BLE 控制器（WiFi 之前）");
    ESP_LOGI(TAG, "========================================");
    
    // 步骤1: 释放 Classic BT 内存（节省 30-40KB 内部 RAM）
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ 已释放 Classic BT 内存（约 30-40KB）");
    } else {
        ESP_LOGW(TAG, "⚠️  释放 Classic BT 内存失败: %d (可能已释放)", ret);
    }
    
    // 步骤2: 提前初始化 NimBLE port（包括 BLE 控制器）
    // 这会为 BLE 控制器分配所需的内部 SRAM，之后 WiFi 再使用剩余内存
    // 注意：这只是初始化 NimBLE port，不会启动 BLE 广播
    ESP_LOGI(TAG, "🔵 初始化 NimBLE port...");
    ret = nimble_port_init();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "✅ NimBLE port 初始化成功");
        ESP_LOGI(TAG, "   - BLE 控制器内存已预留");
        ESP_LOGI(TAG, "   - BLE 广播将在 IDLE 状态时启动");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "✓ NimBLE port 已初始化");
    } else {
        ESP_LOGE(TAG, "❌ NimBLE port 初始化失败: %d", ret);
        ESP_LOGE(TAG, "   BLE 功能将不可用");
    }
    
    ESP_LOGI(TAG, "========================================");

    // Launch the application
    // WiFi 将在 Application::Start() 中初始化
    // BLE 广播将在进入 IDLE 状态时由 Application 启动
    auto& app = Application::GetInstance();
    app.Start();
}
