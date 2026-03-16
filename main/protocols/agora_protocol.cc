#include "agora_protocol.h"
#include "agora_channel.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <agora_rtc_api.h>
#include "assets/lang_config.h"

#define TAG "AgoraProtocol"

static constexpr int kSampleRate       = 16000;
static constexpr int kFrameDurationMs  = 60;
static constexpr int kPcmFrameMs       = 20;   // SDK G722 expects 20ms PCM frames
static constexpr int kPcmFrameSamples  = kSampleRate * kPcmFrameMs / 1000;   // 320
static constexpr int kPcmFrameBytes    = kPcmFrameSamples * sizeof(int16_t);  // 640
static constexpr int kProtocolVersion  = 3;

AgoraProtocol::AgoraProtocol() {
    server_sample_rate_    = kSampleRate;
    server_frame_duration_ = kFrameDurationMs;

    pcm_silence_20ms_.resize(kPcmFrameBytes, 0);
    ESP_LOGI(TAG, "Cached 20ms PCM silence: %u bytes", (unsigned)pcm_silence_20ms_.size());

    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(kSampleRate, 1, kFrameDurationMs);
    ESP_LOGI(TAG, "OPUS decoder ready (fallback for wake-word packets)");
}

AgoraProtocol::~AgoraProtocol() {
    tts_playing_ = false;
    if (silence_timer_) {
        xTimerStop(silence_timer_, 0);
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
    tts_playing_ = false;
    if (silence_timer_) {
        xTimerStop(silence_timer_, 0);
    }
    last_audio_send_us_ = 0;
    gap_dtx_count_ = 0;
    channel_.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Continuous silence sender (TTS + listening gap-fill)
// ─────────────────────────────────────────────────────────────────────────────

void AgoraProtocol::SilenceTimerCallback(TimerHandle_t timer) {
    auto* self = static_cast<AgoraProtocol*>(pvTimerGetTimerID(timer));
    if (!self || !self->channel_ || !self->channel_->IsConnected()) {
        return;
    }

    bool should_send = false;
    if (self->tts_playing_) {
        should_send = true;
    } else if (self->last_audio_send_us_ > 0) {
        int64_t gap_us = esp_timer_get_time() - self->last_audio_send_us_;
        static constexpr int64_t kMaxGapUs = 30000000LL; // 30s safety cap
        if (gap_us > kFrameDurationMs * 1000 && gap_us < kMaxGapUs) {
            should_send = true;
            self->gap_dtx_count_++;
            if (self->gap_dtx_count_ == 1 || self->gap_dtx_count_ % 50 == 0) {
                ESP_LOGI(TAG, "Gap-fill DTX #%u (gap=%lldms)",
                         (unsigned)self->gap_dtx_count_, (long long)(gap_us / 1000));
            }
        }
    }

    if (should_send) {
        auto* agora_ch = static_cast<AgoraChannel*>(self->channel_.get());
        for (int i = 0; i < 3; ++i) {
            agora_ch->SendNativeAudio(self->pcm_silence_20ms_.data(),
                                      self->pcm_silence_20ms_.size(),
                                      AUDIO_DATA_TYPE_PCM);
        }
    }
}

void AgoraProtocol::StartSilenceSender() {
    if (tts_playing_) return;
    tts_playing_ = true;
    ESP_LOGI(TAG, "TTS silence mode ON");
}

void AgoraProtocol::StopSilenceSender() {
    if (!tts_playing_) return;
    tts_playing_ = false;
    last_audio_send_us_ = esp_timer_get_time();
    ESP_LOGI(TAG, "TTS silence mode OFF — gap-fill DTX will take over");
}

void AgoraProtocol::NotifyPlaybackComplete() {
    if (!tts_stop_pending_) return;
    tts_stop_pending_ = false;
    ESP_LOGI(TAG, "Playback queue drained — stopping silence sender, mic echo guard lifted");
    StopSilenceSender();
}

bool AgoraProtocol::SendHello() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", kProtocolVersion);

    cJSON* features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);

    cJSON_AddStringToObject(root, "transport", "agora");

    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", kSampleRate);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", kFrameDurationMs);
    cJSON_AddStringToObject(audio_params, "audio_channel", "native");
    cJSON_AddItemToObject(root, "audio_params", audio_params);

    auto* json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Sending hello: %s", message.c_str());
    return SendText(message);
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

    if (tts_playing_) {
        return true;
    }

    last_audio_send_us_ = esp_timer_get_time();
    if (gap_dtx_count_ > 0) {
        ESP_LOGI(TAG, "Real audio resumed after %u gap-fill DTX frames",
                 (unsigned)gap_dtx_count_);
        gap_dtx_count_ = 0;
    }

    audio_packets_sent_++;

    auto* agora_ch = static_cast<AgoraChannel*>(channel_.get());

    if (packet->is_pcm) {
        const int16_t* pcm = reinterpret_cast<const int16_t*>(packet->payload.data());
        size_t total_samples = packet->payload.size() / sizeof(int16_t);

        if (audio_packets_sent_ <= 5 || audio_packets_sent_ % 50 == 0) {
            ESP_LOGI(TAG, "SendAudio #%d: %u PCM samples → SDK G722 (direct)",
                     audio_packets_sent_, (unsigned)total_samples);
        }

        for (size_t off = 0; off + kPcmFrameSamples <= total_samples; off += kPcmFrameSamples) {
            if (!agora_ch->SendNativeAudio(&pcm[off], kPcmFrameBytes, AUDIO_DATA_TYPE_PCM)) {
                ESP_LOGW(TAG, "SendNativeAudio failed at offset %u", (unsigned)off);
                return false;
            }
        }
    } else {
        std::vector<int16_t> pcm;
        if (!opus_decoder_->Decode(std::move(packet->payload), pcm)) {
            ESP_LOGW(TAG, "SendAudio: OPUS decode failed, dropping frame");
            return false;
        }

        if (audio_packets_sent_ <= 5 || audio_packets_sent_ % 50 == 0) {
            ESP_LOGI(TAG, "SendAudio #%d: %u PCM samples → SDK G722 (via OPUS decode)",
                     audio_packets_sent_, (unsigned)pcm.size());
        }

        for (size_t off = 0; off + kPcmFrameSamples <= pcm.size(); off += kPcmFrameSamples) {
            if (!agora_ch->SendNativeAudio(&pcm[off], kPcmFrameBytes, AUDIO_DATA_TYPE_PCM)) {
                ESP_LOGW(TAG, "SendNativeAudio failed at offset %u", (unsigned)off);
                return false;
            }
        }
    }
    return true;
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
            auto* type_item = cJSON_GetObjectItem(root, "type");
            const char* msg_type = (type_item && cJSON_IsString(type_item))
                                       ? type_item->valuestring : "";

            if (strcmp(msg_type, "hello") == 0) {
                // Server hello — parse session_id and audio params (same
                // semantics as WebsocketProtocol::ParseServerHello).
                auto* sid = cJSON_GetObjectItem(root, "session_id");
                if (cJSON_IsString(sid) && sid->valuestring[0]) {
                    ESP_LOGI(TAG, "Server hello session_id: %s", sid->valuestring);
                    session_id_ = sid->valuestring;
                }
                auto* ap = cJSON_GetObjectItem(root, "audio_params");
                if (cJSON_IsObject(ap)) {
                    auto* sr = cJSON_GetObjectItem(ap, "sample_rate");
                    if (cJSON_IsNumber(sr)) server_sample_rate_ = sr->valueint;
                    auto* fd = cJSON_GetObjectItem(ap, "frame_duration");
                    if (cJSON_IsNumber(fd)) server_frame_duration_ = fd->valueint;
                }
                ESP_LOGI(TAG, "Server hello parsed (sr=%d, fd=%d)",
                         server_sample_rate_, server_frame_duration_);
                // Don't forward hello to Application.
            } else {
                // Adopt session_id from any server message (fallback if
                // server doesn't send a hello response).
                auto* sid_item = cJSON_GetObjectItem(root, "session_id");
                if (sid_item && cJSON_IsString(sid_item) && sid_item->valuestring) {
                    std::string server_sid = sid_item->valuestring;
                    if (!server_sid.empty() && server_sid != session_id_) {
                        ESP_LOGI(TAG, "Adopting server session_id: %s -> %s",
                                 session_id_.c_str(), server_sid.c_str());
                        session_id_ = server_sid;
                    }
                }

                // Drive silence sender on tts:start / tts:stop.
                if (strcmp(msg_type, "tts") == 0) {
                    auto* state_item = cJSON_GetObjectItem(root, "state");
                    if (state_item && cJSON_IsString(state_item)) {
                        if (strcmp(state_item->valuestring, "start") == 0) {
                            tts_stop_pending_ = false;
                            StartSilenceSender();
                        } else if (strcmp(state_item->valuestring, "stop") == 0) {
                            StopSilenceSender();
                        }
                    }
                }

                if (on_incoming_json_) {
                    on_incoming_json_(root);
                }
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

    session_id_ = agora_ch->channel();
    channel_ = std::move(agora_ch);

    // Send the same hello handshake that WebSocket uses so the server can
    // initialise its STT/TTS session with the correct audio parameters.
    // Without this the server has no codec/format context for the Agora
    // channel and may fail to process audio in Round 2+.
    if (!SendHello()) {
        ESP_LOGE(TAG, "Failed to send hello to server");
    }

    last_audio_send_us_ = 0;
    gap_dtx_count_ = 0;
    if (!silence_timer_) {
        silence_timer_ = xTimerCreate("agora_sil", pdMS_TO_TICKS(kFrameDurationMs),
                                      pdTRUE, this, SilenceTimerCallback);
    }
    if (silence_timer_) {
        xTimerStart(silence_timer_, 0);
        ESP_LOGI(TAG, "Silence timer started (%dms interval, covers TTS + gap-fill)",
                 kFrameDurationMs);
    }

    if (on_audio_channel_opened_) {
        on_audio_channel_opened_();
    }
    return true;
}
