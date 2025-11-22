#include "wifi_station.h"
#include <cstring>
#include <algorithm>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs.h>
#include "nvs_flash.h"
#include <esp_netif.h>
#include <esp_system.h>
#include "ssid_manager.h"

#define TAG "WifiStation"
#define WIFI_EVENT_CONNECTED BIT0
#define MAX_RECONNECT_COUNT 5

WifiStation& WifiStation::GetInstance() {
    static WifiStation instance;
    return instance;
}

WifiStation::WifiStation() {
    // Create the event group
    event_group_ = xEventGroupCreate();

    // 读取配置
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
    }
    err = nvs_get_i8(nvs, "max_tx_power", &max_tx_power_);
    if (err != ESP_OK) {
        max_tx_power_ = 0;
    }
    err = nvs_get_u8(nvs, "remember_bssid", &remember_bssid_);
    if (err != ESP_OK) {
        remember_bssid_ = 0;
    }
    nvs_close(nvs);
}

WifiStation::~WifiStation() {
    vEventGroupDelete(event_group_);
}

void WifiStation::AddAuth(const std::string &&ssid, const std::string &&password) {
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.AddSsid(ssid, password);
}

void WifiStation::Stop() {
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }

    esp_wifi_scan_stop();
    
    // 取消注册事件处理程序
    if (instance_any_id_ != nullptr) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_));
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_ != nullptr) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_));
        instance_got_ip_ = nullptr;
    }

    // ✅ 修复：清除连接状态标志位
    xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
    ESP_LOGI(TAG, "✓ 已清除 WiFi 连接状态标志");
    
    // 清空状态信息
    ssid_ = "";
    password_ = "";
    ip_address_ = "";
    reconnect_count_ = 0;
    is_scanning_ = false;  // 重置扫描状态
    is_timer_running_ = false;  // 重置定时器状态

    // Reset the WiFi stack
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    if (station_netif_ != nullptr) {
        // TODO: esp_netif_destroy will cause crash
        // esp_netif_destroy(station_netif_);
        station_netif_ = nullptr;
    }
}

void WifiStation::OnScanBegin(std::function<void()> on_scan_begin) {
    on_scan_begin_ = on_scan_begin;
}

void WifiStation::OnConnect(std::function<void(const std::string& ssid)> on_connect) {
    on_connect_ = on_connect;
}

void WifiStation::OnConnected(std::function<void(const std::string& ssid)> on_connected) {
    on_connected_ = on_connected;
}

void WifiStation::Start() {
    // ✅ 修复：确保初始状态下连接标志位为清除状态
    xEventGroupClearBits(event_group_, WIFI_EVENT_CONNECTED);
    
    // Initialize the TCP/IP stack (if not already initialized)
    esp_err_t netif_init_err = esp_netif_init();
    if (netif_init_err != ESP_OK && netif_init_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(netif_init_err);
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiStation::WifiEventHandler,
                                                        this,
                                                        &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiStation::IpEventHandler,
                                                        this,
                                                        &instance_got_ip_));

    // Create the default event loop (check if already exists)
    station_netif_ = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (station_netif_ == nullptr) {
        ESP_LOGI("WifiStation", "创建新的 STA netif");
        station_netif_ = esp_netif_create_default_wifi_sta();
    } else {
        ESP_LOGI("WifiStation", "使用已存在的 STA netif（可能在 APSTA 模式下）");
    }

    // Check if WiFi is already initialized
    wifi_mode_t current_mode = WIFI_MODE_NULL;
    esp_err_t mode_err = esp_wifi_get_mode(&current_mode);
    bool wifi_initialized = (mode_err == ESP_OK);
    
    if (!wifi_initialized) {
        ESP_LOGI("WifiStation", "初始化 WiFi 驱动（纯 STA 模式）");
        // Initialize the WiFi stack in station mode
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        cfg.nvs_enable = false;
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGI("WifiStation", "WiFi 已初始化，当前模式: %d", current_mode);
        // If in AP mode, switch to APSTA; if already APSTA or STA, keep it
        if (current_mode == WIFI_MODE_AP) {
            ESP_LOGI("WifiStation", "从 AP 模式切换到 APSTA 模式");
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        } else if (current_mode == WIFI_MODE_APSTA) {
            ESP_LOGI("WifiStation", "已在 APSTA 模式，无需切换");
        } else if (current_mode == WIFI_MODE_STA) {
            ESP_LOGI("WifiStation", "已在 STA 模式，无需切换");
        }
        // WiFi is already started, no need to start again
    }

    if (max_tx_power_ != 0) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(max_tx_power_));
    }

    // Setup the timer to scan WiFi (if not already created)
    if (timer_handle_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto* this_ = static_cast<WifiStation*>(arg);
                this_->is_timer_running_ = false;  // 清除定时器运行标志
                if (!this_->is_scanning_) {  // 只有在没有扫描时才启动新扫描
                    esp_wifi_scan_start(nullptr, false);
                } else {
                    ESP_LOGD(TAG, "Scan already in progress, skipping");
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "WiFiScanTimer",
            .skip_unhandled_events = true
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
    }

    // ⚠️ 修复：在 WiFi 已初始化的情况下（APSTA 模式），手动触发首次扫描
    // 因为不会有 WIFI_EVENT_STA_START 事件
    if (wifi_initialized) {
        ESP_LOGI(TAG, "WiFi 已运行，手动触发首次扫描");
        is_scanning_ = true;  // 设置扫描状态标志
        esp_wifi_scan_start(nullptr, false);
        if (on_scan_begin_) {
            on_scan_begin_();
        }
    }
}

bool WifiStation::WaitForConnected(int timeout_ms) {
    auto bits = xEventGroupWaitBits(event_group_, WIFI_EVENT_CONNECTED, pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    return (bits & WIFI_EVENT_CONNECTED) != 0;
}

void WifiStation::HandleScanResult() {
    is_scanning_ = false;  // 清除扫描状态标志
    
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_num * sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);
    // sort by rssi descending
    std::sort(ap_records, ap_records + ap_num, [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
        return a.rssi > b.rssi;
    });

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    for (int i = 0; i < ap_num; i++) {
        auto ap_record = ap_records[i];
        auto it = std::find_if(ssid_list.begin(), ssid_list.end(), [ap_record](const SsidItem& item) {
            return strcmp((char *)ap_record.ssid, item.ssid.c_str()) == 0;
        });
        if (it != ssid_list.end()) {
            ESP_LOGI(TAG, "Found AP: %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x, RSSI: %d, Channel: %d, Authmode: %d",
                (char *)ap_record.ssid, 
                ap_record.bssid[0], ap_record.bssid[1], ap_record.bssid[2],
                ap_record.bssid[3], ap_record.bssid[4], ap_record.bssid[5],
                ap_record.rssi, ap_record.primary, ap_record.authmode);
            WifiApRecord record = {
                .ssid = it->ssid,
                .password = it->password,
                .channel = ap_record.primary,
                .authmode = ap_record.authmode
            };
            memcpy(record.bssid, ap_record.bssid, 6);
            connect_queue_.push_back(record);
        }
    }
    free(ap_records);

    if (connect_queue_.empty()) {
        ESP_LOGD(TAG, "No matching AP found, will retry in 10 seconds");  // 改为 DEBUG 级别
        
        // 只有在定时器未运行时才启动
        if (!is_timer_running_) {
            esp_err_t err = esp_timer_start_once(timer_handle_, 10 * 1000);
            if (err == ESP_OK) {
                is_timer_running_ = true;
                ESP_LOGD(TAG, "Scan timer started");
            } else {
                ESP_LOGW(TAG, "Failed to start scan timer: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGD(TAG, "Scan timer already running, skip");
        }
        return;
    }

    StartConnect();
}

void WifiStation::StartConnect() {
    auto ap_record = connect_queue_.front();
    connect_queue_.erase(connect_queue_.begin());
    ssid_ = ap_record.ssid;
    password_ = ap_record.password;

    if (on_connect_) {
        on_connect_(ssid_);
    }

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, ap_record.ssid.c_str());
    strcpy((char *)wifi_config.sta.password, ap_record.password.c_str());
    if (remember_bssid_) {
        wifi_config.sta.channel = ap_record.channel;
        memcpy(wifi_config.sta.bssid, ap_record.bssid, 6);
        wifi_config.sta.bssid_set = true;
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    reconnect_count_ = 0;
    ESP_ERROR_CHECK(esp_wifi_connect());
}

int8_t WifiStation::GetRssi() {
    // Get station info
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    
    // ⚠️ 修复：不使用 ESP_ERROR_CHECK，避免在 WiFi 断开时崩溃
    // 如果 WiFi 未初始化或未连接，返回一个无效的 RSSI 值
    if (err != ESP_OK) {
        ESP_LOGW("WifiStation", "GetRssi() failed: %s (0x%x)", esp_err_to_name(err), err);
        return -127;  // 返回最小值表示信号不可用
    }
    
    return ap_info.rssi;
}

uint8_t WifiStation::GetChannel() {
    // Get station info
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    
    // ⚠️ 修复：不使用 ESP_ERROR_CHECK，避免在 WiFi 断开时崩溃
    // 如果 WiFi 未初始化或未连接，返回 0
    if (err != ESP_OK) {
        ESP_LOGW("WifiStation", "GetChannel() failed: %s (0x%x)", esp_err_to_name(err), err);
        return 0;  // 返回 0 表示信道不可用
    }
    
    return ap_info.primary;
}

bool WifiStation::IsConnected() {
    return xEventGroupGetBits(event_group_) & WIFI_EVENT_CONNECTED;
}

void WifiStation::SetPowerSaveMode(bool enabled) {
    ESP_ERROR_CHECK(esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE));
}

// Static event handler functions
void WifiStation::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_START) {
        this_->is_scanning_ = true;  // 设置扫描状态标志
        esp_wifi_scan_start(nullptr, false);
        if (this_->on_scan_begin_) {
            this_->on_scan_begin_();
        }
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        this_->HandleScanResult();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);
        if (this_->reconnect_count_ < MAX_RECONNECT_COUNT) {
            esp_wifi_connect();
            this_->reconnect_count_++;
            ESP_LOGI(TAG, "Reconnecting %s (attempt %d / %d)", this_->ssid_.c_str(), this_->reconnect_count_, MAX_RECONNECT_COUNT);
            return;
        }

        if (!this_->connect_queue_.empty()) {
            this_->StartConnect();
            return;
        }
        
        ESP_LOGD(TAG, "No more AP to connect, scheduling next scan");  // 改为 DEBUG 级别
        
        // 只有在定时器未运行时才启动
        if (!this_->is_timer_running_) {
            esp_err_t err = esp_timer_start_once(this_->timer_handle_, 10 * 1000);
            if (err == ESP_OK) {
                this_->is_timer_running_ = true;
                ESP_LOGD(TAG, "Scan timer started after disconnect");
            } else {
                ESP_LOGW(TAG, "Failed to start scan timer: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGD(TAG, "Scan timer already running after disconnect");
        }
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
    }
}

void WifiStation::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    this_->ip_address_ = ip_address;
    ESP_LOGI(TAG, "Got IP: %s", this_->ip_address_.c_str());
    
    xEventGroupSetBits(this_->event_group_, WIFI_EVENT_CONNECTED);
    if (this_->on_connected_) {
        this_->on_connected_(this_->ssid_);
    }
    this_->connect_queue_.clear();
    this_->reconnect_count_ = 0;
}
