#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"
#include "ble_wifi_provisioner.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_network.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_err.h>

#include <font_awesome.h>
#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include "afsk_demod.h"

static const char *TAG = "WifiBoard";

WifiBoard::WifiBoard() {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::EnterWifiConfigMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    // ====== 第一步：启动 Soft AP WiFi 配网 ======
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "第1步: 启动 Soft AP WiFi 配网");
    ESP_LOGI(TAG, "========================================");
    
    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("Xiaozhi");
    wifi_ap.Start();

    ESP_LOGI(TAG, "✓ Soft AP 已启动: %s", wifi_ap.GetSsid().c_str());

    // 立即播报语音提示（不等待扫描完成）
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_ap.GetSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_ap.GetWebServerUrl();
    hint += "\n\n";
    
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear", Lang::Sounds::OGG_WIFICONFIG);
    ESP_LOGI(TAG, "✓ 配网提示音已播报");

    // ====== 预扫描 WiFi 网络（与语音播报并行）+ 定期刷新 ======
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "开始预扫描 WiFi 网络（后台持续刷新）...");
    ESP_LOGI(TAG, "========================================");
    
    // 在后台启动WiFi扫描任务，持续刷新缓存
    xTaskCreate([](void* arg) {
        const char* task_tag = "WiFiPeriodicScan";
        
        // 等待300ms确保Soft AP完全启动
        vTaskDelay(pdMS_TO_TICKS(300));
        
        ESP_LOGI(task_tag, "启动WiFi周期性扫描任务（每30秒刷新一次）");
        
        while (true) {
            // Soft AP模式下，WiFi已经在运行（AP+STA模式）
            // 发起全信道扫描
            wifi_scan_config_t scan_config = {
                .ssid = nullptr,
                .bssid = nullptr,
                .channel = 0,  // 0 = 扫描所有信道
                .show_hidden = false,
                .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                .scan_time = {
                    .active = {
                        .min = 0,  // 使用默认值，适配BLE+WiFi共存
                        .max = 0   // 使用默认值，适配BLE+WiFi共存
                    }
                }
            };
            
            esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
            if (ret == ESP_OK) {
                ESP_LOGI(task_tag, "✓ WiFi扫描已启动（全信道扫描）");
                
                // 等待扫描完成（最多10秒）
                for (int i = 0; i < 100; i++) {
                    uint16_t ap_count = 0;
                    ret = esp_wifi_scan_get_ap_num(&ap_count);
                    if (ret == ESP_OK && ap_count > 0) {
                        ESP_LOGI(task_tag, "✓ WiFi扫描完成，发现 %d 个网络（缓存有效期约60秒）", ap_count);
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            } else {
                ESP_LOGW(task_tag, "WiFi扫描启动失败: %s（30秒后重试）", esp_err_to_name(ret));
            }
            
            // 每30秒刷新一次缓存，确保BLE始终能获取到最新列表
            vTaskDelay(pdMS_TO_TICKS(30000));
        }
        
        // 永不退出（配网模式持续运行）
    }, "wifi_periodic_scan", 3072, NULL, 5, NULL);

    // ====== 第二步：延迟启动 BLE 配网（15秒后）======
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "将在 15 秒后启动 BLE 配网服务...");
    ESP_LOGI(TAG, "========================================");
    
    // 创建后台任务来延迟启动 BLE
    xTaskCreate([](void* arg) {
        // 等待15秒，给用户时间先尝试 Soft AP
        vTaskDelay(pdMS_TO_TICKS(15000));
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "第2步: 启动 BLE WiFi 配网");
        ESP_LOGI(TAG, "========================================");
        
        auto& provisioner = BLEWiFiProvisioner::GetInstance();
        
        // 设置配网成功回调
        provisioner.SetProvisionSuccessCallback([](const std::string& ssid, const std::string& password) {
            ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
            ESP_LOGI(TAG, "║   ✅ BLE WiFi配网成功！                ║");
            ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
            ESP_LOGI(TAG, "SSID: %s", ssid.c_str());
            ESP_LOGI(TAG, "设备将在2秒后重启...");
            
            // 配网模式下，配网成功后自动重启设备
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        });
        
        // 设置配网失败回调
        provisioner.SetProvisionFailureCallback([](const std::string& error_message) {
            ESP_LOGE(TAG, "╔════════════════════════════════════════╗");
            ESP_LOGE(TAG, "║   ❌ BLE WiFi配网失败                  ║");
            ESP_LOGE(TAG, "╚════════════════════════════════════════╝");
            ESP_LOGE(TAG, "错误: %s", error_message.c_str());
        });
        
        // 初始化并启动 BLE WiFi 配网
        // BLE 配网会自动使用 WifiStation 缓存的扫描结果
        if (provisioner.Initialize("ESP32-PLAUD")) {
            if (provisioner.Start()) {
                ESP_LOGI(TAG, "✓ BLE配网服务已启动");
                ESP_LOGI(TAG, "✓ BLE将复用Soft AP的WiFi扫描结果");
            } else {
                ESP_LOGE(TAG, "❌ BLE广播启动失败");
            }
        } else {
            ESP_LOGE(TAG, "❌ BLE配网服务初始化失败");
        }
        
        ESP_LOGI(TAG, "========================================");
        
        // 任务完成，删除自己
        vTaskDelete(NULL);
    }, "ble_delayed_start", 4096, NULL, 5, NULL);

    #if CONFIG_USE_ACOUSTIC_WIFI_PROVISIONING
    auto display = Board::GetInstance().GetDisplay();
    auto codec = Board::GetInstance().GetAudioCodec();
    int channel = 1;
    if (codec) {
        channel = codec->input_channels();
    }
    ESP_LOGI(TAG, "Start receiving WiFi credentials from audio, input channels: %d", channel);
    audio_wifi_config::ReceiveWifiCredentialsFromAudio(&application, &wifi_ap, display, channel);
    #endif
    
    // Wait forever until reset after configuration
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void WifiBoard::StartNetwork() {
    // User can press BOOT button while starting to enter WiFi configuration mode
    if (wifi_config_mode_) {
        EnterWifiConfigMode();
        return;
    }

    // If no WiFi SSID is configured, enter WiFi configuration mode
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    if (ssid_list.empty()) {
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }

    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.OnScanBegin([this]() {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
    });
    wifi_station.OnConnect([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECT_TO;
        notification += ssid;
        notification += "...";
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.OnConnected([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECTED_TO;
        notification += ssid;
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.Start();

    // Try to connect to WiFi, if failed, launch the WiFi configuration AP
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }
    
    // ====== WiFi连接成功后，启动BLE服务（用于其他功能设置）======
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WiFi连接成功，启动BLE服务...");
    ESP_LOGI(TAG, "========================================");
    
    // 启动BLE服务，用于常规功能配置（非配网模式）
    auto& provisioner = BLEWiFiProvisioner::GetInstance();
    
    // 在正常模式下，配网成功回调不需要重启设备
    provisioner.SetProvisionSuccessCallback([](const std::string& ssid, const std::string& password) {
        ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   ✅ BLE WiFi配网成功（已更新WiFi）    ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
        ESP_LOGI(TAG, "新SSID: %s", ssid.c_str());
        ESP_LOGW(TAG, "⚠️  WiFi配置已更新，重启后生效");
        // 注意：不自动重启，让用户决定何时重启
    });
    
    provisioner.SetProvisionFailureCallback([](const std::string& error_message) {
        ESP_LOGE(TAG, "╔════════════════════════════════════════╗");
        ESP_LOGE(TAG, "║   ❌ BLE WiFi配置失败                  ║");
        ESP_LOGE(TAG, "╚════════════════════════════════════════╝");
        ESP_LOGE(TAG, "错误: %s", error_message.c_str());
    });
    
    if (provisioner.Initialize("ESP32-PLAUD")) {
        if (provisioner.Start()) {
            ESP_LOGI(TAG, "✓ BLE服务已启动（正常模式）");
            ESP_LOGI(TAG, "✓ BLE可用于功能配置和设置");
        } else {
            ESP_LOGW(TAG, "⚠️  BLE服务启动失败（不影响正常功能）");
        }
    } else {
        ESP_LOGW(TAG, "⚠️  BLE服务初始化失败（不影响正常功能）");
    }
    
    ESP_LOGI(TAG, "========================================");
}

NetworkInterface* WifiBoard::GetNetwork() {
    static EspNetwork network;
    return &network;
}

const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;
    }
    auto& wifi_station = WifiStation::GetInstance();
    if (!wifi_station.IsConnected()) {
        return FONT_AWESOME_WIFI_SLASH;
    }
    
    // ⚠️ 安全性：GetRssi() 现在不会崩溃，但会返回 -127 表示失败
    int8_t rssi = wifi_station.GetRssi();
    
    // 检查 RSSI 是否有效
    if (rssi == -127 || rssi == 0) {
        // RSSI 无效，WiFi 可能已断开
        return FONT_AWESOME_WIFI_SLASH;
    }
    
    // 根据 RSSI 强度返回对应图标
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    } else {
        return FONT_AWESOME_WIFI_WEAK;
    }
}

std::string WifiBoard::GetBoardJson() {
    // Set the board type for OTA
    auto& wifi_station = WifiStation::GetInstance();
    std::string board_json = R"({)";
    board_json += R"("type":")" + std::string(BOARD_TYPE) + R"(",)";
    board_json += R"("name":")" + std::string(BOARD_NAME) + R"(",)";
    
    // ⚠️ 安全性：只有在 WiFi 已连接时才添加网络信息
    // GetRssi() 和 GetChannel() 现在不会崩溃，但在断开时返回无效值
    if (!wifi_config_mode_ && wifi_station.IsConnected()) {
        board_json += R"("ssid":")" + wifi_station.GetSsid() + R"(",)";
        board_json += R"("rssi":)" + std::to_string(wifi_station.GetRssi()) + R"(,)";
        board_json += R"("channel":)" + std::to_string(wifi_station.GetChannel()) + R"(,)";
        board_json += R"("ip":")" + wifi_station.GetIpAddress() + R"(",)";
    }
    
    board_json += R"("mac":")" + SystemInfo::GetMacAddress() + R"(")";
    board_json += R"(})";
    return board_json;
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.SetPowerSaveMode(enabled);
}

void WifiBoard::ResetWifiConfiguration() {
    // Set a flag and reboot the device to enter the network configuration mode
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Reboot the device
    esp_restart();
}

std::string WifiBoard::GetDeviceStatusJson() {
    /*
     * 返回设备状态JSON
     * 
     * 返回的JSON结构如下：
     * {
     *     "audio_speaker": {
     *         "volume": 70
     *     },
     *     "screen": {
     *         "brightness": 100,
     *         "theme": "light"
     *     },
     *     "battery": {
     *         "level": 50,
     *         "charging": true
     *     },
     *     "network": {
     *         "type": "wifi",
     *         "ssid": "Xiaozhi",
     *         "rssi": -60
     *     },
     *     "chip": {
     *         "temperature": 25
     *     }
     * }
     */
    auto& board = Board::GetInstance();
    auto root = cJSON_CreateObject();

    // Audio speaker
    auto audio_speaker = cJSON_CreateObject();
    auto audio_codec = board.GetAudioCodec();
    if (audio_codec) {
        cJSON_AddNumberToObject(audio_speaker, "volume", audio_codec->output_volume());
    }
    cJSON_AddItemToObject(root, "audio_speaker", audio_speaker);

    // Screen brightness
    auto backlight = board.GetBacklight();
    auto screen = cJSON_CreateObject();
    if (backlight) {
        cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
    }
    auto display = board.GetDisplay();
    if (display && display->height() > 64) { // For LCD display only
        auto theme = display->GetTheme();
        if (theme != nullptr) {
            cJSON_AddStringToObject(screen, "theme", theme->name().c_str());
        }
    }
    cJSON_AddItemToObject(root, "screen", screen);

    // Battery
    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        cJSON* battery = cJSON_CreateObject();
        cJSON_AddNumberToObject(battery, "level", battery_level);
        cJSON_AddBoolToObject(battery, "charging", charging);
        cJSON_AddItemToObject(root, "battery", battery);
    }

    // Network
    auto network = cJSON_CreateObject();
    auto& wifi_station = WifiStation::GetInstance();
    cJSON_AddStringToObject(network, "type", "wifi");
    cJSON_AddStringToObject(network, "ssid", wifi_station.GetSsid().c_str());
    int rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        cJSON_AddStringToObject(network, "signal", "strong");
    } else if (rssi >= -70) {
        cJSON_AddStringToObject(network, "signal", "medium");
    } else {
        cJSON_AddStringToObject(network, "signal", "weak");
    }
    cJSON_AddItemToObject(root, "network", network);

    // Chip
    float esp32temp = 0.0f;
    if (board.GetTemperature(esp32temp)) {
        auto chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "temperature", esp32temp);
        cJSON_AddItemToObject(root, "chip", chip);
    }

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return json;
}
