#include "ota.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>

#define TAG "Ota"

// 官方 OTA 服务器域名
#define OFFICIAL_OTA_DOMAIN "api.tenclass.net"
#define OFFICIAL_OTA_DOMAIN_2 "2662r3426b.vicp.fun"


Ota::Ota() {
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    // Read Serial Number from efuse user_data
    uint8_t serial_number[33] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_number, 32 * 8) == ESP_OK) {
        if (serial_number[0] == 0) {
            has_serial_number_ = false;
        } else {
            serial_number_ = std::string(reinterpret_cast<char*>(serial_number), 32);
            has_serial_number_ = true;
        }
    }
#endif
}

Ota::~Ota() {
}

std::string Ota::GetCheckVersionUrl() {
    // 强制使用固定的 OTA 地址，忽略 NVS 中的自定义配置
    std::string url = CONFIG_OTA_URL;
    ESP_LOGI(TAG, "Using fixed OTA URL: %s", url.c_str());
    return url;
}

std::unique_ptr<Http> Ota::SetupHttp() {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(0);
    auto user_agent = SystemInfo::GetUserAgent();
    
    // 判断是否是官方服务器（支持多个官方域名）
    std::string ota_url = GetCheckVersionUrl();
    bool is_official_server = (ota_url.find(OFFICIAL_OTA_DOMAIN) != std::string::npos) ||
                              (ota_url.find(OFFICIAL_OTA_DOMAIN_2) != std::string::npos);
    
    http->SetHeader("Activation-Version", has_serial_number_ ? "2" : "1");
    
    // 根据服务器类型设置不同的 Header
    // 官方服务器：使用 MAC 地址作为 Device-Id，UUID 作为 Client-Id（兼容原项目）
    // 非官方服务器：都使用 DeviceId（当前项目逻辑）
    if (is_official_server) {
        http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
        http->SetHeader("Client-Id", board.GetUuid().c_str());
        ESP_LOGI(TAG, "Using official server headers: Device-Id=MAC, Client-Id=UUID");
    } else {
        http->SetHeader("Device-Id", board.GetDeviceId().c_str());
        http->SetHeader("Client-Id", board.GetDeviceId().c_str());
        ESP_LOGI(TAG, "Using custom server headers: Device-Id=DeviceId, Client-Id=DeviceId");
    }
    
    if (has_serial_number_) {
        http->SetHeader("Serial-Number", serial_number_.c_str());
        ESP_LOGI(TAG, "Setup HTTP, User-Agent: %s, Serial-Number: %s", user_agent.c_str(), serial_number_.c_str());
    }
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");

    return http;
}

/* 
 * Specification: https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */
bool Ota::CheckVersion() {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    // Check if there is a new firmware version available
    current_version_ = app_desc->version;
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    std::string url = GetCheckVersionUrl();
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return false;
    }

    // 判断是否是官方服务器（用于日志显示，支持多个官方域名）
    bool is_official_server = (url.find(OFFICIAL_OTA_DOMAIN) != std::string::npos) ||
                              (url.find(OFFICIAL_OTA_DOMAIN_2) != std::string::npos);
    std::string device_id_header = is_official_server ? SystemInfo::GetMacAddress() : board.GetDeviceId();
    std::string client_id_header = is_official_server ? board.GetUuid() : board.GetDeviceId();

    auto http = SetupHttp();

    std::string data = board.GetSystemInfoJson();
    std::string method = data.length() > 0 ? "POST" : "GET";
    
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   📤 OTA 请求详情                                              ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║   URL: %s", url.c_str());
    ESP_LOGI(TAG, "║   方法: %s", method.c_str());
    ESP_LOGI(TAG, "║   官方服务器: %s", is_official_server ? "是" : "否");
    ESP_LOGI(TAG, "║   Device-Id Header: %s", device_id_header.c_str());
    ESP_LOGI(TAG, "║   Client-Id Header: %s", client_id_header.c_str());
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║   📋 请求 Body (POST data):                                    ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════════╝");
    
    // 分段打印请求 body（ESP_LOG 有长度限制）
    if (data.length() > 0) {
        const size_t chunk_size = 200;
        for (size_t i = 0; i < data.length(); i += chunk_size) {
            std::string chunk = data.substr(i, chunk_size);
            ESP_LOGI(TAG, "%s", chunk.c_str());
        }
    } else {
        ESP_LOGI(TAG, "(空)");
    }
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════════════");

    http->SetContent(std::move(data));

    ESP_LOGI(TAG, "正在连接 OTA 服务器...");

    if (!http->Open(method, url)) {
        ESP_LOGE(TAG, "❌ 无法连接到 OTA 服务器");
        ESP_LOGE(TAG, "请检查网络连接和服务器状态");
        return false;
    }

    auto status_code = http->GetStatusCode();
    ESP_LOGI(TAG, "✅ OTA 服务器连接成功, HTTP 状态码: %d", status_code);
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "❌ 服务器返回错误状态码: %d", status_code);
        return false;
    }

    data = http->ReadAll();
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   📥 OTA 服务器响应                                            ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════════╝");
    // 分段打印响应内容
    if (data.length() > 0) {
        const size_t chunk_size = 200;
        for (size_t i = 0; i < data.length(); i += chunk_size) {
            std::string chunk = data.substr(i, chunk_size);
            ESP_LOGI(TAG, "%s", chunk.c_str());
        }
    } else {
        ESP_LOGI(TAG, "(空)");
    }
    ESP_LOGI(TAG, "════════════════════════════════════════════════════════════════");
    http->Close();

    // Response: { "firmware": { "version": "1.0.0", "url": "http://" } }
    // Parse the JSON response and check if the version is newer
    // If it is, set has_new_version_ to true and store the new version and URL
    
    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return false;
    }

    has_activation_code_ = false;
    has_activation_challenge_ = false;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "📱 发现激活信息");
        ESP_LOGI(TAG, "========================================");
        
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(message)) {
            activation_message_ = message->valuestring;
            ESP_LOGI(TAG, "激活消息: %s", activation_message_.c_str());
        }
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            activation_code_ = code->valuestring;
            has_activation_code_ = true;
            ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
            ESP_LOGI(TAG, "║   🔑 激活码（调试输出）               ║");
            ESP_LOGI(TAG, "╠════════════════════════════════════════╣");
            ESP_LOGI(TAG, "║   %s   ║", activation_code_.c_str());
            ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
        }
        cJSON* challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            activation_challenge_ = challenge->valuestring;
            has_activation_challenge_ = true;
            ESP_LOGI(TAG, "挑战码: %s", activation_challenge_.c_str());
        }
        cJSON* timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            activation_timeout_ms_ = timeout_ms->valueint;
            ESP_LOGI(TAG, "激活超时: %d ms", activation_timeout_ms_);
        }
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGI(TAG, "✓ 无需激活（设备已激活或无激活要求）");
    }

    has_mqtt_config_ = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        Settings settings("mqtt", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, mqtt) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_mqtt_config_ = true;
        
        // 打印从 OTA 服务器获取到的 MQTT 配置
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   📡 OTA 返回的 MQTT 配置                                      ║");
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════════╣");
        cJSON *mqtt_endpoint = cJSON_GetObjectItem(mqtt, "endpoint");
        cJSON *mqtt_client_id = cJSON_GetObjectItem(mqtt, "client_id");
        cJSON *mqtt_username = cJSON_GetObjectItem(mqtt, "username");
        cJSON *mqtt_topic = cJSON_GetObjectItem(mqtt, "publish_topic");
        if (cJSON_IsString(mqtt_endpoint)) {
            ESP_LOGI(TAG, "║   Endpoint: %s", mqtt_endpoint->valuestring);
        }
        if (cJSON_IsString(mqtt_client_id)) {
            ESP_LOGI(TAG, "║   Client ID: %s", mqtt_client_id->valuestring);
        }
        if (cJSON_IsString(mqtt_username)) {
            ESP_LOGI(TAG, "║   Username: %s", mqtt_username->valuestring);
        }
        if (cJSON_IsString(mqtt_topic)) {
            ESP_LOGI(TAG, "║   Publish Topic: %s", mqtt_topic->valuestring);
        }
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════════╝");
    } else {
        ESP_LOGI(TAG, "No mqtt section found !");
    }

    has_websocket_config_ = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        // 先打印原始 WebSocket JSON 内容
        char *ws_json_str = cJSON_Print(websocket);
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   📡 OTA 返回的 WebSocket 配置（原始 JSON）                    ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════════╝");
        if (ws_json_str) {
            // 分段打印完整 JSON
            std::string ws_json(ws_json_str);
            const size_t chunk_size = 200;
            for (size_t i = 0; i < ws_json.length(); i += chunk_size) {
                std::string chunk = ws_json.substr(i, chunk_size);
                ESP_LOGI(TAG, "%s", chunk.c_str());
            }
            cJSON_free(ws_json_str);
        }
        ESP_LOGI(TAG, "════════════════════════════════════════════════════════════════");
        
        Settings settings("websocket", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, websocket) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
                ESP_LOGI(TAG, "  💾 保存到 NVS: %s = %s", item->string, item->valuestring);
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
                ESP_LOGI(TAG, "  💾 保存到 NVS: %s = %d", item->string, item->valueint);
            }
        }
        has_websocket_config_ = true;
        
        // 打印解析后的关键配置
        ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   ✅ WebSocket 配置已保存                                      ║");
        ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════════╣");
        cJSON *ws_url = cJSON_GetObjectItem(websocket, "url");
        cJSON *ws_token = cJSON_GetObjectItem(websocket, "token");
        cJSON *ws_version = cJSON_GetObjectItem(websocket, "version");
        if (cJSON_IsString(ws_url)) {
            ESP_LOGI(TAG, "║   URL: %s", ws_url->valuestring);
        }
        if (cJSON_IsString(ws_token)) {
            // Token 可能很长，只显示前 50 个字符
            size_t token_len = strlen(ws_token->valuestring);
            if (token_len > 50) {
                ESP_LOGI(TAG, "║   Token: %.50s... (共 %zu 字符)", ws_token->valuestring, token_len);
            } else {
                ESP_LOGI(TAG, "║   Token: %s", ws_token->valuestring);
            }
        }
        if (cJSON_IsNumber(ws_version)) {
            ESP_LOGI(TAG, "║   Version: %d", ws_version->valueint);
        }
        ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════════╝");
    } else {
        ESP_LOGW(TAG, "⚠️  OTA 响应中没有 websocket 配置！");
        ESP_LOGW(TAG, "    如需使用 WebSocket，请确保服务器返回 websocket 字段");
    }

    has_server_time_ = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");
        
        if (cJSON_IsNumber(timestamp)) {
            // 设置系统时间
            struct timeval tv;
            double ts = timestamp->valuedouble;
            
            // 如果有时区偏移，计算本地时间
            if (cJSON_IsNumber(timezone_offset)) {
                ts += (timezone_offset->valueint * 60 * 1000); // 转换分钟为毫秒
            }
            
            tv.tv_sec = (time_t)(ts / 1000);  // 转换毫秒为秒
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;  // 剩余的毫秒转换为微秒
            settimeofday(&tv, NULL);
            has_server_time_ = true;
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found!");
    }

    has_new_version_ = false;
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        cJSON *version = cJSON_GetObjectItem(firmware, "version");
        if (cJSON_IsString(version)) {
            firmware_version_ = version->valuestring;
        }
        cJSON *url = cJSON_GetObjectItem(firmware, "url");
        if (cJSON_IsString(url)) {
            firmware_url_ = url->valuestring;
        }

        if (cJSON_IsString(version) && cJSON_IsString(url)) {
            // Check if the version is newer, for example, 0.1.0 is newer than 0.0.1
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            if (has_new_version_) {
                ESP_LOGI(TAG, "New version available: %s", firmware_version_.c_str());
            } else {
                ESP_LOGI(TAG, "Current is the latest version");
            }
            // If the force flag is set to 1, the given version is forced to be installed
            cJSON *force = cJSON_GetObjectItem(firmware, "force");
            if (cJSON_IsNumber(force) && force->valueint == 1) {
                has_new_version_ = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found!");
    }

    cJSON_Delete(root);
    return true;
}

void Ota::MarkCurrentVersionValid() {
    auto partition = esp_ota_get_running_partition();
    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping");
        return;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

bool Ota::Upgrade(const std::string& firmware_url) {
    ESP_LOGI(TAG, "Upgrading firmware from %s", firmware_url.c_str());
    esp_ota_handle_t update_handle = 0;
    auto update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", update_partition->label, update_partition->address);
    bool image_header_checked = false;
    std::string image_header;

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    if (!http->Open("GET", firmware_url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return false;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to get firmware, status code: %d", http->GetStatusCode());
        return false;
    }

    size_t content_length = http->GetBodyLength();
    if (content_length == 0) {
        ESP_LOGE(TAG, "Failed to get content length");
        return false;
    }

    char buffer[512];
    size_t total_read = 0, recent_read = 0;
    auto last_calc_time = esp_timer_get_time();
    while (true) {
        int ret = http->Read(buffer, sizeof(buffer));
        if (ret < 0) {
            ESP_LOGE(TAG, "Failed to read HTTP data: %s", esp_err_to_name(ret));
            return false;
        }

        // Calculate speed and progress every second
        recent_read += ret;
        total_read += ret;
        if (esp_timer_get_time() - last_calc_time >= 1000000 || ret == 0) {
            size_t progress = total_read * 100 / content_length;
            ESP_LOGI(TAG, "Progress: %u%% (%u/%u), Speed: %uB/s", progress, total_read, content_length, recent_read);
            if (upgrade_callback_) {
                upgrade_callback_(progress, recent_read);
            }
            last_calc_time = esp_timer_get_time();
            recent_read = 0;
        }

        if (ret == 0) {
            break;
        }

        if (!image_header_checked) {
            image_header.append(buffer, ret);
            if (image_header.size() >= sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                esp_app_desc_t new_app_info;
                memcpy(&new_app_info, image_header.data() + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));
                
                auto current_version = esp_app_get_description()->version;
                ESP_LOGI(TAG, "Current version: %s, New version: %s", current_version, new_app_info.version);

                if (esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle)) {
                    esp_ota_abort(update_handle);
                    ESP_LOGE(TAG, "Failed to begin OTA");
                    return false;
                }

                image_header_checked = true;
                std::string().swap(image_header);
            }
        }
        auto err = esp_ota_write(update_handle, buffer, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write OTA data: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            return false;
        }
    }
    http->Close();

    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Firmware upgrade successful");
    return true;
}

bool Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback) {
    upgrade_callback_ = callback;
    return Upgrade(firmware_url_);
}

bool Ota::StartUpgradeFromUrl(const std::string& url, std::function<void(int progress, size_t speed)> callback) {
    upgrade_callback_ = callback;
    return Upgrade(url);
}

std::vector<int> Ota::ParseVersion(const std::string& version) {
    std::vector<int> versionNumbers;
    std::stringstream ss(version);
    std::string segment;
    
    while (std::getline(ss, segment, '.')) {
        versionNumbers.push_back(std::stoi(segment));
    }
    
    return versionNumbers;
}

bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    std::vector<int> current = ParseVersion(currentVersion);
    std::vector<int> newer = ParseVersion(newVersion);
    
    for (size_t i = 0; i < std::min(current.size(), newer.size()); ++i) {
        if (newer[i] > current[i]) {
            return true;
        } else if (newer[i] < current[i]) {
            return false;
        }
    }
    
    return newer.size() > current.size();
}

std::string Ota::GetActivationPayload() {
    if (!has_serial_number_) {
        return "{}";
    }

    std::string hmac_hex;
#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32]; // SHA-256 输出为32字节
    
    // 使用Key0计算HMAC
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0, (uint8_t*)activation_challenge_.data(), activation_challenge_.size(), hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        return "{}";
    }

    for (size_t i = 0; i < sizeof(hmac_result); i++) {
        char buffer[3];
        sprintf(buffer, "%02x", hmac_result[i]);
        hmac_hex += buffer;
    }
#endif

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number_.c_str());
    cJSON_AddStringToObject(payload, "challenge", activation_challenge_.c_str());
    cJSON_AddStringToObject(payload, "hmac", hmac_hex.c_str());
    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload: %s", json.c_str());
    return json;
}

esp_err_t Ota::Activate() {
    if (!has_activation_challenge_) {
        ESP_LOGW(TAG, "⚠️  无激活挑战码，跳过激活");
        return ESP_FAIL;
    }

    std::string url = GetCheckVersionUrl();
    if (url.back() != '/') {
        url += "/activate";
    } else {
        url += "activate";
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "正在发送激活请求...");
    ESP_LOGI(TAG, "激活 URL: %s", url.c_str());
    ESP_LOGI(TAG, "========================================");

    auto http = SetupHttp();

    std::string data = GetActivationPayload();
    http->SetContent(std::move(data));

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "❌ 无法连接到激活服务器");
        ESP_LOGE(TAG, "请检查网络连接");
        return ESP_FAIL;
    }
    
    auto status_code = http->GetStatusCode();
    ESP_LOGI(TAG, "激活响应状态码: %d", status_code);
    
    if (status_code == 202) {
        ESP_LOGW(TAG, "⏳ 激活请求已接受，等待服务器处理...");
        return ESP_ERR_TIMEOUT;
    }
    if (status_code != 200) {
        std::string response_body = http->ReadAll();
        ESP_LOGE(TAG, "❌ 激活失败");
        ESP_LOGE(TAG, "状态码: %d", status_code);
        ESP_LOGE(TAG, "响应内容: %s", response_body.c_str());
        return ESP_FAIL;
    }

    std::string response_body = http->ReadAll();
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "✅ 激活成功！");
    ESP_LOGI(TAG, "响应内容: %s", response_body.c_str());
    ESP_LOGI(TAG, "========================================");
    return ESP_OK;
}
