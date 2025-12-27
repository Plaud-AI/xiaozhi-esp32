#include "websocket_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"
#include "settings.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "WS"

// 官方 OTA 服务器域名
#define OFFICIAL_OTA_DOMAIN "api.tenclass.net"
#define OFFICIAL_OTA_DOMAIN_2 "2662r3426b.vicp.fun"

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed
    return true;
}

bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        ESP_LOGW(TAG, "⚠️ SendAudio: WebSocket not connected");
        return false;
    }

    static int audio_send_count = 0;
    audio_send_count++;
    
    bool result = false;
    size_t payload_size = packet->payload.size();

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
        auto bp2 = (BinaryProtocol2*)serialized.data();
        bp2->version = htons(version_);
        bp2->type = 0;
        bp2->reserved = 0;
        bp2->timestamp = htonl(packet->timestamp);
        bp2->payload_size = htonl(packet->payload.size());
        memcpy(bp2->payload, packet->payload.data(), packet->payload.size());

        result = websocket_->Send(serialized.data(), serialized.size(), true);
    } else if (version_ == 3) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());
        auto bp3 = (BinaryProtocol3*)serialized.data();
        bp3->type = 0;
        bp3->reserved = 0;
        bp3->payload_size = htons(packet->payload.size());
        memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

        result = websocket_->Send(serialized.data(), serialized.size(), true);
    } else {
        result = websocket_->Send(packet->payload.data(), packet->payload.size(), true);
    }
    
    // Log every 50 sends
    if (audio_send_count % 50 == 1) {
        ESP_LOGI(TAG, "🔊 WS SendAudio #%d: %zu bytes, version=%d, result=%d", 
                 audio_send_count, payload_size, version_, result);
    }
    
    if (!result) {
        ESP_LOGW(TAG, "⚠️ WS SendAudio #%d failed!", audio_send_count);
    }
    
    return result;
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() {
    websocket_.reset();
}

bool WebsocketProtocol::IsOfficialServer(const std::string& ota_url) {
    // 判断是否是官方服务器：
    // 1. OTA URL 包含官方域名 api.tenclass.net
    // 2. 或者 OTA URL 包含官方域名 2662r3426b.vicp.fun
    // 3. 或者使用默认的官方 OTA 配置
    return ota_url.find(OFFICIAL_OTA_DOMAIN) != std::string::npos ||
           ota_url.find(OFFICIAL_OTA_DOMAIN_2) != std::string::npos;
}

bool WebsocketProtocol::OpenAudioChannel() {
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    std::string token = settings.GetString("token");
    int version = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    // 如果 NVS 中没有 WebSocket URL，使用默认值
    if (url.empty()) {
        url = "ws://18.143.177.88:8000/xiaozhi/v1/";
        ESP_LOGW(TAG, "WebSocket URL not configured in NVS, using default: %s", url.c_str());
    }
    
    current_url_ = url;

    // 读取 OTA URL 来判断是否是官方服务器
    // 先尝试从 "system" namespace 读取，再尝试从 "wifi" namespace 读取（兼容原项目）
    std::string ota_url;
    Settings system_settings("system", false);
    ota_url = system_settings.GetString("ota_url", "");
    if (ota_url.empty()) {
        Settings wifi_settings("wifi", false);
        ota_url = wifi_settings.GetString("ota_url", CONFIG_OTA_URL);
    }
    is_official_server_ = IsOfficialServer(ota_url);

    ESP_LOGI(TAG, "WebSocket configuration - URL: %s, Version: %d, Official: %s", 
             url.c_str(), version_, is_official_server_ ? "Yes" : "No");
    
    error_occurred_ = false;

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    if (!token.empty()) {
        // If token not has a space, add "Bearer " prefix
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    
    // 根据服务器类型设置不同的 Header
    // 官方服务器：使用 MAC 地址作为 Device-Id，DeviceId 作为 Client-Id（兼容原项目）
    // 非官方服务器：都使用 DeviceId（当前项目逻辑）
    if (is_official_server_) {
        websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
        websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
        ESP_LOGI(TAG, "Using official server headers: Device-Id=MAC, Client-Id=UUID");
    } else {
        websocket_->SetHeader("Device-Id", Board::GetInstance().GetDeviceId().c_str());
        websocket_->SetHeader("Client-Id", Board::GetInstance().GetDeviceId().c_str());
        ESP_LOGI(TAG, "Using custom server headers: Device-Id=DeviceId, Client-Id=DeviceId");
    }

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            static int audio_packet_count = 0;
            static int64_t first_packet_time = 0;
            audio_packet_count++;
            
            int64_t now = esp_timer_get_time() / 1000; // ms
            if (audio_packet_count == 1) {
                first_packet_time = now;
                ESP_LOGI(TAG, "🎵 First audio packet: %u bytes, type=0x%02x", (unsigned int)len, (uint8_t)data[0]);
            }
            
            // 每 10 个包打印一次日志
            if (audio_packet_count % 10 == 1) {
                int64_t elapsed = now - first_packet_time;
                ESP_LOGI(TAG, "🎵 Audio #%d: %u bytes, elapsed=%ldms, type=0x%02x, official=%d", 
                         audio_packet_count, (unsigned int)len, (long)elapsed, (uint8_t)data[0], is_official_server_);
            }
            if (on_incoming_audio_ != nullptr) {
                if (version_ == 2) {
                    BinaryProtocol2* bp2 = (BinaryProtocol2*)data;
                    bp2->version = ntohs(bp2->version);
                    bp2->type = ntohs(bp2->type);
                    bp2->timestamp = ntohl(bp2->timestamp);
                    bp2->payload_size = ntohl(bp2->payload_size);
                    auto payload = (uint8_t*)bp2->payload;
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = bp2->timestamp,
                        .payload = std::vector<uint8_t>(payload, payload + bp2->payload_size)
                    }));
                } else if (version_ == 3) {
                    BinaryProtocol3* bp3 = (BinaryProtocol3*)data;
                    bp3->type = bp3->type;
                    bp3->payload_size = ntohs(bp3->payload_size);
                    auto payload = (uint8_t*)bp3->payload;
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = 0,
                        .payload = std::vector<uint8_t>(payload, payload + bp3->payload_size)
                    }));
                } else {
                    // 默认版本：根据服务器类型使用不同的解析逻辑
                    if (is_official_server_) {
                        // 官方服务器：直接使用原始数据（兼容原项目）
                        on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                            .sample_rate = server_sample_rate_,
                            .frame_duration = server_frame_duration_,
                            .timestamp = 0,
                            .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                        }));
                    } else {
                        // 非官方服务器：解析服务端新增的 16 字节头部
                        // 头部格式：type(1) + message_tag(1) + payload_size(4, big-endian) + reserved(10) = 16 bytes
                        if (len >= sizeof(AudioPacketHeader)) {
                            const AudioPacketHeader* header = (const AudioPacketHeader*)data;
                            if (header->type == 0x01) {
                                // 这是带有 16 字节头部的音频包
                                // payload_size 是大端序，需要转换
                                uint32_t payload_size = ntohl(header->payload_size);
                                
                                // 验证数据包大小
                                if (len >= sizeof(AudioPacketHeader) + payload_size) {
                                    auto payload = (uint8_t*)header->payload;
                                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                                        .sample_rate = server_sample_rate_,
                                        .frame_duration = server_frame_duration_,
                                        .timestamp = 0,
                                        .payload = std::vector<uint8_t>(payload, payload + payload_size)
                                    }));
                                } else {
                                    ESP_LOGE(TAG, "Audio packet size mismatch: len=%u, header_size=%u, payload_size=%lu", 
                                             (unsigned int)len, (unsigned int)sizeof(AudioPacketHeader), payload_size);
                                }
                            } else {
                                // 非音频消息类型，按原始数据处理
                                on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                                    .sample_rate = server_sample_rate_,
                                    .frame_duration = server_frame_duration_,
                                    .timestamp = 0,
                                    .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                                }));
                            }
                        } else {
                            // 数据太短，按原始数据处理（向后兼容）
                            on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                                .sample_rate = server_sample_rate_,
                                .frame_duration = server_frame_duration_,
                                .timestamp = 0,
                                .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                            }));
                        }
                    }
                }
            }
        } else {
            // Parse JSON data
            ESP_LOGI(TAG, "📩 Received JSON: %.*s", (len > 200 ? 200 : (int)len), data);
            auto root = cJSON_Parse(data);
            auto type = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type)) {
                ESP_LOGD(TAG, "   Message type: %s", type->valuestring);
                if (strcmp(type->valuestring, "hello") == 0) {
                    ParseServerHello(root);
                } else {
                    if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Missing message type, data: %s", data);
            }
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "╔════════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   🔗 正在连接 WebSocket 服务器                                 ║");
    ESP_LOGI(TAG, "╠════════════════════════════════════════════════════════════════╣");
    ESP_LOGI(TAG, "║   URL: %s", url.c_str());
    ESP_LOGI(TAG, "║   Protocol Version: %d", version_);
    ESP_LOGI(TAG, "║   Token: %s", token.empty() ? "(无)" : "(已配置)");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════════════════════╝");
    
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "❌ Failed to connect to websocket server: %s", url.c_str());
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ WebSocket 连接成功: %s", url.c_str());

    // Send hello message to describe the client
    auto message = GetHelloMessage();
    if (!SendText(message)) {
        return false;
    }

    // Wait for server hello
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

std::string WebsocketProtocol::GetHelloMessage() {
    // keys: message type, version, audio_params (format, sample_rate, channels)
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
    }

    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}
