#include "agora_protocol.h"
#include "agora_channel.h"

#include <cJSON.h>
#include <esp_log.h>
#include <opus_encoder.h>
#include "assets/lang_config.h"

#define TAG "AgoraProtocol"

static constexpr int kSampleRate     = 16000;
static constexpr int kFrameDurationMs = 60;

AgoraProtocol::AgoraProtocol() {
    server_sample_rate_    = kSampleRate;
    server_frame_duration_ = kFrameDurationMs;

    // Pre-encode one frame of PCM silence into OPUS and cache it.
    // This cached frame is sent repeatedly during TTS to keep the RTC
    // audio stream alive without leaking microphone echo.
    const int frame_samples = kSampleRate * kFrameDurationMs / 1000; // 960
    OpusEncoderWrapper enc(kSampleRate, 1, kFrameDurationMs);
    std::vector<int16_t> silence(frame_samples, 0);
    enc.Encode(std::move(silence), opus_silence_frame_);
    ESP_LOGI(TAG, "Cached OPUS silence frame: %u bytes", (unsigned)opus_silence_frame_.size());
}

AgoraProtocol::~AgoraProtocol() {
    StopSilenceSender();
    if (silence_timer_) {
        xTimerDelete(silence_timer_, 0);
        silence_timer_ = nullptr;
    }
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
    StopSilenceSender();
    channel_.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// TTS silence sender
// ─────────────────────────────────────────────────────────────────────────────

void AgoraProtocol::SilenceTimerCallback(TimerHandle_t timer) {
    auto* self = static_cast<AgoraProtocol*>(pvTimerGetTimerID(timer));
    if (!self || !self->tts_playing_ || !self->channel_ || !self->channel_->IsConnected()) {
        return;
    }
    self->channel_->SendBinary(self->opus_silence_frame_.data(),
                               self->opus_silence_frame_.size());
}

void AgoraProtocol::StartSilenceSender() {
    if (tts_playing_) return;
    tts_playing_ = true;

    if (!silence_timer_) {
        silence_timer_ = xTimerCreate("agora_sil", pdMS_TO_TICKS(kFrameDurationMs),
                                      pdTRUE, this, SilenceTimerCallback);
    }
    if (silence_timer_) {
        xTimerStart(silence_timer_, 0);
        ESP_LOGI(TAG, "Silence sender started (TTS playing, %d ms interval)",
                 kFrameDurationMs);
    }
}

void AgoraProtocol::StopSilenceSender() {
    if (!tts_playing_) return;
    tts_playing_ = false;

    if (silence_timer_) {
        xTimerStop(silence_timer_, 0);
    }
    ESP_LOGI(TAG, "Silence sender stopped (TTS ended)");
}

bool AgoraProtocol::SendText(const std::string& text) {
    if (!channel_ || !channel_->IsConnected()) {
        ESP_LOGW(TAG, "SendText: channel not ready (channel=%p, connected=%d)",
                 channel_.get(), channel_ ? channel_->IsConnected() : 0);
        return false;
    }
    ESP_LOGI(TAG, "SendText: %.*s", (int)std::min(text.size(), (size_t)200), text.c_str());
    if (!channel_->SendText(text)) {
        ESP_LOGE(TAG, "Failed to send text via Agora data stream");
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    return true;
}

bool AgoraProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (!channel_ || !channel_->IsConnected()) {
        ESP_LOGW(TAG, "SendAudio: channel not ready (ch=%p, connected=%d)",
                 channel_.get(), channel_ ? channel_->IsConnected() : 0);
        return false;
    }

    // During TTS the silence timer handles audio; drop real mic data
    // to prevent echo from reaching the server.
    if (tts_playing_) {
        return true;
    }

    audio_packets_sent_++;
    if (audio_packets_sent_ <= 5 || audio_packets_sent_ % 50 == 0) {
        ESP_LOGI(TAG, "SendAudio #%d: %u bytes (OPUS)",
                 audio_packets_sent_, (unsigned)packet->payload.size());
    }
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
            // The Agora AI Agent assigns its own session_id and embeds it in
            // every response message. Unlike the WebSocket protocol (which has
            // an explicit hello handshake), we must extract and adopt the
            // server-side session_id from the first incoming JSON so that all
            // subsequent control messages (listen:start, listen:stop, …) carry
            // the session_id the server actually recognises.
            auto* sid_item = cJSON_GetObjectItem(root, "session_id");
            if (sid_item && cJSON_IsString(sid_item) && sid_item->valuestring) {
                std::string server_sid = sid_item->valuestring;
                if (!server_sid.empty() && server_sid != session_id_) {
                    ESP_LOGI(TAG, "Adopting server session_id: %s -> %s",
                             session_id_.c_str(), server_sid.c_str());
                    session_id_ = server_sid;
                }
            }
            // Detect tts:start / tts:stop to drive the silence sender.
            auto* type_item = cJSON_GetObjectItem(root, "type");
            if (type_item && cJSON_IsString(type_item) &&
                strcmp(type_item->valuestring, "tts") == 0) {
                auto* state_item = cJSON_GetObjectItem(root, "state");
                if (state_item && cJSON_IsString(state_item)) {
                    if (strcmp(state_item->valuestring, "start") == 0) {
                        StartSilenceSender();
                    } else if (strcmp(state_item->valuestring, "stop") == 0) {
                        StopSilenceSender();
                    }
                }
            }

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
    audio_packets_sent_ = 0;

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
