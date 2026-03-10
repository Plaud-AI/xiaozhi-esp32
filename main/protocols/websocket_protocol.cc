#include "websocket_protocol.h"
#include "websocket_channel.h"
#include "application.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "WS"

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed.
    return true;
}

bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (channel_ == nullptr || !channel_->IsConnected()) {
        return false;
    }

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
        auto bp2 = (BinaryProtocol2*)serialized.data();
        bp2->version      = htons(version_);
        bp2->type         = 0;
        bp2->reserved     = 0;
        bp2->timestamp    = htonl(packet->timestamp);
        bp2->payload_size = htonl(packet->payload.size());
        memcpy(bp2->payload, packet->payload.data(), packet->payload.size());
        return channel_->SendBinary(serialized.data(), serialized.size());
    } else if (version_ == 3) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());
        auto bp3 = (BinaryProtocol3*)serialized.data();
        bp3->type         = 0;
        bp3->reserved     = 0;
        bp3->payload_size = htons(packet->payload.size());
        memcpy(bp3->payload, packet->payload.data(), packet->payload.size());
        return channel_->SendBinary(serialized.data(), serialized.size());
    } else {
        return channel_->SendBinary(packet->payload.data(), packet->payload.size());
    }
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (channel_ == nullptr || !channel_->IsConnected()) {
        return false;
    }

    if (!channel_->SendText(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return channel_ != nullptr && channel_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() {
    channel_.reset();
}

bool WebsocketProtocol::OpenAudioChannel() {
    error_occurred_ = false;

    // Create and connect the WebSocket transport channel.
    auto ws_channel = std::make_unique<WebsocketChannel>();

    ws_channel->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ws_channel->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            static int audio_packet_count = 0;
            static int64_t first_packet_time = 0;
            audio_packet_count++;

            int64_t now = esp_timer_get_time() / 1000; // ms
            if (audio_packet_count == 1) {
                first_packet_time = now;
                ESP_LOGI(TAG, "First audio packet: %u bytes, type=0x%02x",
                         (unsigned int)len, (uint8_t)data[0]);
            }
            if (audio_packet_count % 10 == 1) {
                int64_t elapsed = now - first_packet_time;
                ESP_LOGI(TAG, "Audio #%d: %u bytes, elapsed=%ldms, type=0x%02x, official=%d",
                         audio_packet_count, (unsigned int)len, (long)elapsed,
                         (uint8_t)data[0], is_official_server_);
            }

            if (on_incoming_audio_ != nullptr) {
                if (version_ == 2) {
                    BinaryProtocol2* bp2 = (BinaryProtocol2*)data;
                    bp2->version      = ntohs(bp2->version);
                    bp2->type         = ntohs(bp2->type);
                    bp2->timestamp    = ntohl(bp2->timestamp);
                    bp2->payload_size = ntohl(bp2->payload_size);
                    auto payload = (uint8_t*)bp2->payload;
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate    = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp      = bp2->timestamp,
                        .payload        = std::vector<uint8_t>(payload, payload + bp2->payload_size)
                    }));
                } else if (version_ == 3) {
                    BinaryProtocol3* bp3 = (BinaryProtocol3*)data;
                    bp3->payload_size = ntohs(bp3->payload_size);
                    auto payload = (uint8_t*)bp3->payload;
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate    = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp      = 0,
                        .payload        = std::vector<uint8_t>(payload, payload + bp3->payload_size)
                    }));
                } else {
                    if (is_official_server_) {
                        on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                            .sample_rate    = server_sample_rate_,
                            .frame_duration = server_frame_duration_,
                            .timestamp      = 0,
                            .payload        = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                        }));
                    } else {
                        if (len >= sizeof(AudioPacketHeader)) {
                            const AudioPacketHeader* header = (const AudioPacketHeader*)data;
                            if (header->type == 0x01) {
                                uint32_t payload_size = ntohl(header->payload_size);
                                if (len >= sizeof(AudioPacketHeader) + payload_size) {
                                    auto payload = (uint8_t*)header->payload;
                                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                                        .sample_rate    = server_sample_rate_,
                                        .frame_duration = server_frame_duration_,
                                        .timestamp      = 0,
                                        .payload        = std::vector<uint8_t>(payload, payload + payload_size)
                                    }));
                                } else {
                                    ESP_LOGE(TAG, "Audio packet size mismatch: len=%u, header_size=%u, payload_size=%lu",
                                             (unsigned int)len, (unsigned int)sizeof(AudioPacketHeader), payload_size);
                                }
                            } else {
                                on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                                    .sample_rate    = server_sample_rate_,
                                    .frame_duration = server_frame_duration_,
                                    .timestamp      = 0,
                                    .payload        = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                                }));
                            }
                        } else {
                            on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                                .sample_rate    = server_sample_rate_,
                                .frame_duration = server_frame_duration_,
                                .timestamp      = 0,
                                .payload        = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                            }));
                        }
                    }
                }
            }
        } else {
            ESP_LOGI(TAG, "Received JSON: %.*s", (len > 200 ? 200 : (int)len), data);
            auto root = cJSON_Parse(data);
            auto type = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type)) {
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

    if (!ws_channel->Connect()) {
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    // Cache transport-level properties for use during audio serialization.
    version_           = ws_channel->version();
    is_official_server_ = ws_channel->is_official_server();

    channel_ = std::move(ws_channel);

    // Application-level handshake: send hello and wait for server hello.
    auto message = GetHelloMessage();
    if (!SendText(message)) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(event_group_handle_,
                                           WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
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
