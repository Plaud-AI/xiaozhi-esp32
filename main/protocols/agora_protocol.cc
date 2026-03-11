#include "agora_protocol.h"
#include "agora_channel.h"

#include <cJSON.h>
#include <esp_log.h>
#include "assets/lang_config.h"

#define TAG "AgoraProtocol"

// Agora Conversational AI uses 16 kHz OPUS (same as the xiaozhi pipeline).
static constexpr int kSampleRate     = 16000;
static constexpr int kFrameDurationMs = 60;

AgoraProtocol::AgoraProtocol() {
    server_sample_rate_    = kSampleRate;
    server_frame_duration_ = kFrameDurationMs;
}

bool AgoraProtocol::Start() {
    // No pre-connection needed; Agora joins on first OpenAudioChannel().
    return true;
}

bool AgoraProtocol::IsAudioChannelOpened() const {
    return channel_ != nullptr && channel_->IsConnected() &&
           !error_occurred_ && !IsTimeout();
}

void AgoraProtocol::CloseAudioChannel() {
    channel_.reset();
}

bool AgoraProtocol::SendText(const std::string& text) {
    if (!channel_ || !channel_->IsConnected()) {
        return false;
    }
    if (!channel_->SendText(text)) {
        ESP_LOGE(TAG, "Failed to send text via Agora data stream");
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    return true;
}

bool AgoraProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (!channel_ || !channel_->IsConnected()) {
        return false;
    }
    // packet->payload already contains a raw OPUS frame; send it directly.
    return channel_->SendBinary(packet->payload.data(), packet->payload.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Incoming data handler (registered with the channel)
// ─────────────────────────────────────────────────────────────────────────────

void AgoraProtocol::HandleIncomingData(const char* data, size_t len, bool binary) {
    last_incoming_time_ = std::chrono::steady_clock::now();

    if (binary) {
        // Incoming OPUS audio frame from the AI agent.
        if (on_incoming_audio_) {
            on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                .sample_rate    = server_sample_rate_,
                .frame_duration = server_frame_duration_,
                .timestamp      = 0,
                .payload        = std::vector<uint8_t>(
                                      reinterpret_cast<const uint8_t*>(data),
                                      reinterpret_cast<const uint8_t*>(data) + len)
            }));
        }
    } else {
        // Incoming JSON message from the AI agent via data stream.
        ESP_LOGI(TAG, "Received JSON: %.*s", (int)std::min(len, (size_t)200), data);
        auto* root = cJSON_Parse(data);
        if (root) {
            if (on_incoming_json_) {
                on_incoming_json_(root);
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGE(TAG, "Failed to parse incoming JSON");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenAudioChannel
// ─────────────────────────────────────────────────────────────────────────────

bool AgoraProtocol::OpenAudioChannel() {
    error_occurred_ = false;

    auto agora_ch = std::make_unique<AgoraChannel>();

    agora_ch->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Agora channel disconnected");
        if (on_audio_channel_closed_) {
            on_audio_channel_closed_();
        }
    });

    agora_ch->OnData([this](const char* data, size_t len, bool binary) {
        HandleIncomingData(data, len, binary);
    });

    if (!agora_ch->Connect()) {
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    // session_id_ is not used in Agora's conversation model (the AI Agent
    // manages the session server-side), but Protocol's default JSON helpers
    // embed it; set it to the channel name as a stable identifier.
    session_id_ = agora_ch->channel();

    channel_ = std::move(agora_ch);

    // Agora Conversational AI manages the conversation automatically;
    // no hello / server-hello exchange is needed.
    if (on_audio_channel_opened_) {
        on_audio_channel_opened_();
    }
    return true;
}
