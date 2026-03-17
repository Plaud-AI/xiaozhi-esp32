#include "agora_channel.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>

#define TAG "AgoraChannel"

// ─────────────────────────────────────────────────────────────────────────────
// Static singleton
// ─────────────────────────────────────────────────────────────────────────────

AgoraChannel* AgoraChannel::s_instance_ = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Static C-callback bridges
// ─────────────────────────────────────────────────────────────────────────────

void AgoraChannel::S_OnJoinSuccess(connection_id_t conn_id, uint32_t uid, int elapsed_ms) {
    if (s_instance_) s_instance_->OnJoinSuccess(conn_id, uid, elapsed_ms);
}

void AgoraChannel::S_OnConnectionLost(connection_id_t conn_id) {
    if (s_instance_) s_instance_->OnConnectionLost(conn_id);
}

void AgoraChannel::S_OnReconnecting(connection_id_t conn_id) {
    if (s_instance_) s_instance_->OnReconnecting(conn_id);
}

void AgoraChannel::S_OnRejoinSuccess(connection_id_t conn_id, uint32_t uid, int elapsed_ms) {
    if (s_instance_) s_instance_->OnRejoinSuccess(conn_id, uid, elapsed_ms);
}

void AgoraChannel::S_OnAudioData(connection_id_t /*conn_id*/, uint32_t /*uid*/,
                                  uint16_t /*sent_ts*/,
                                  const void* data, size_t len,
                                  const audio_frame_info_t* info) {
    if (s_instance_) s_instance_->OnAudioData(data, len, info);
}

void AgoraChannel::S_OnStreamMessage(connection_id_t /*conn_id*/, uint32_t /*uid*/,
                                      int /*stream_id*/, const char* data,
                                      size_t length, uint64_t /*sent_ts*/) {
    if (s_instance_) s_instance_->OnStreamMessage(data, length);
}

void AgoraChannel::S_OnError(connection_id_t /*conn_id*/, int code, const char* msg) {
    if (s_instance_) s_instance_->OnError(code, msg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

AgoraChannel::AgoraChannel() {
    event_group_ = xEventGroupCreate();
    s_instance_  = this;
}

AgoraChannel::~AgoraChannel() {
    if (connected_) {
        Disconnect();
    }
    vEventGroupDelete(event_group_);
    if (s_instance_ == this) {
        s_instance_ = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Config — only the four credentials the device needs to join a channel
// ─────────────────────────────────────────────────────────────────────────────

bool AgoraChannel::ReadConfig() {
    Settings s("agora", false);
    app_id_  = s.GetString("app_id");
    channel_ = s.GetString("channel");
    token_   = s.GetString("token");   // may be empty when auth is disabled
    uid_     = (uint32_t)s.GetInt("uid", 0);

    if (app_id_.empty()) {
        ESP_LOGE(TAG, "agora.app_id not configured — set via OTA server response");
        return false;
    }
    if (channel_.empty()) {
        ESP_LOGE(TAG, "agora.channel not configured — set via OTA server response");
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel::Connect
// ─────────────────────────────────────────────────────────────────────────────

bool AgoraChannel::Connect() {
    if (!ReadConfig()) {
        return false;
    }

    // Register event callbacks.
    agora_rtc_event_handler_t handler{};
    handler.on_join_channel_success   = S_OnJoinSuccess;
    handler.on_connection_lost        = S_OnConnectionLost;
    handler.on_reconnecting           = S_OnReconnecting;
    handler.on_rejoin_channel_success = S_OnRejoinSuccess;
    handler.on_audio_data             = S_OnAudioData;
    handler.on_stream_message         = S_OnStreamMessage;
    handler.on_error                  = S_OnError;

    rtc_service_option_t service_opt{};
    service_opt.area_code           = AREA_CODE_GLOB;
    service_opt.log_cfg.log_disable = false;
    service_opt.log_cfg.log_level   = RTC_LOG_WARNING;
    service_opt.domain_limit        = false;

    ESP_LOGI(TAG, "Initialising Agora SDK (app_id=%s)", app_id_.c_str());
    int rc = agora_rtc_init(app_id_.c_str(), &handler, &service_opt);
    if (rc < 0) {
        ESP_LOGE(TAG, "agora_rtc_init failed: %s", agora_rtc_err_2_str(rc));
        return false;
    }

    rc = agora_rtc_create_connection(&conn_id_);
    if (rc < 0) {
        ESP_LOGE(TAG, "agora_rtc_create_connection failed: %s", agora_rtc_err_2_str(rc));
        agora_rtc_fini();
        return false;
    }

    // Use G722 codec (16kHz wideband) for uplink encoding.
    // SDK encodes PCM→G722 internally using libiot-audio-codec.a;
    // the server's Agora SDK decodes G722→PCM and delivers via audio callback.
    // OPUS codec is unavailable (causes abort), but G722 works correctly.
    rtc_channel_options_t ch_opts{};
    ch_opts.auto_subscribe_audio             = true;
    ch_opts.auto_subscribe_video             = false;
    ch_opts.audio_codec_opt.audio_codec_type = AUDIO_CODEC_TYPE_G722;
    ch_opts.audio_codec_opt.pcm_sample_rate  = 16000;
    ch_opts.audio_codec_opt.pcm_channel_num  = 1;

    const char* token_ptr = token_.empty() ? nullptr : token_.c_str();

    ESP_LOGI(TAG, "Joining channel '%s' (uid=%lu)...", channel_.c_str(), (unsigned long)uid_);
    rc = agora_rtc_join_channel(conn_id_, channel_.c_str(), uid_, token_ptr, &ch_opts);
    if (rc < 0) {
        ESP_LOGE(TAG, "agora_rtc_join_channel failed: %s", agora_rtc_err_2_str(rc));
        agora_rtc_destroy_connection(conn_id_);
        agora_rtc_fini();
        conn_id_ = CONNECTION_ID_INVALID;
        return false;
    }

    // Wait for the join confirmation from the Agora service.
    EventBits_t bits = xEventGroupWaitBits(event_group_,
                                           AGORA_CHANNEL_JOIN_SUCCESS_EVENT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(AGORA_CHANNEL_JOIN_TIMEOUT_MS));
    if (!(bits & AGORA_CHANNEL_JOIN_SUCCESS_EVENT)) {
        ESP_LOGE(TAG, "Timed out waiting for join_channel_success");
        agora_rtc_leave_channel(conn_id_);
        agora_rtc_destroy_connection(conn_id_);
        agora_rtc_fini();
        conn_id_ = CONNECTION_ID_INVALID;
        return false;
    }

    CreateDataStream();

    connected_ = true;
    ESP_LOGI(TAG, "Agora channel connected (channel=%s)", channel_.c_str());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: (re)create the data stream used for JSON control messages
// ─────────────────────────────────────────────────────────────────────────────

bool AgoraChannel::CreateDataStream() {
    stream_id_ = -1;
    // reliable=true:  SDK retransmits on loss, guarantees delivery within 5s.
    //                 Ensures detect/start/stop JSON reaches the server.
    // ordered=false:  No head-of-line blocking; if one packet is delayed,
    //                 subsequent ones are delivered immediately.
    int rc = agora_rtc_create_data_stream(conn_id_, &stream_id_,
                                          /*reliable=*/true, /*ordered=*/false);
    if (rc < 0) {
        ESP_LOGW(TAG, "Data stream creation failed (%s); JSON messages unavailable",
                 agora_rtc_err_2_str(rc));
        stream_id_ = -1;
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel::Disconnect
// ─────────────────────────────────────────────────────────────────────────────

void AgoraChannel::Disconnect() {
    if (!connected_ && conn_id_ == CONNECTION_ID_INVALID) {
        return;
    }
    connected_  = false;
    stream_id_ = -1;

    if (conn_id_ != CONNECTION_ID_INVALID) {
        agora_rtc_leave_channel(conn_id_);
        agora_rtc_destroy_connection(conn_id_);
        conn_id_ = CONNECTION_ID_INVALID;
    }
    agora_rtc_fini();
    ESP_LOGI(TAG, "Agora channel disconnected");
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel::IsConnected / SendBinary / SendText
// ─────────────────────────────────────────────────────────────────────────────

bool AgoraChannel::IsConnected() const {
    return connected_;
}

bool AgoraChannel::SendBinary(const void* data, size_t len) {
    if (!connected_ || conn_id_ == CONNECTION_ID_INVALID || stream_id_ < 0) {
        return false;
    }
    uint8_t buf[1 + len];
    buf[0] = 0x01;
    memcpy(buf + 1, data, len);
    int rc = agora_rtc_send_stream_message(conn_id_, stream_id_,
                                           reinterpret_cast<const char*>(buf),
                                           1 + len);
    if (rc < 0) {
        ESP_LOGW(TAG, "agora_rtc_send_stream_message(binary) failed: %s",
                 agora_rtc_err_2_str(rc));
        return false;
    }
    return true;
}

bool AgoraChannel::SendNativeAudio(const void* data, size_t len, int data_type) {
    if (!connected_ || conn_id_ == CONNECTION_ID_INVALID) {
        return false;
    }
    audio_frame_info_t info{};
    info.data_type = static_cast<audio_data_type_e>(data_type);
    int rc = agora_rtc_send_audio_data(conn_id_, data, len, &info);
    if (rc < 0) {
        ESP_LOGW(TAG, "agora_rtc_send_audio_data failed: %s (type=%d, len=%u)",
                 agora_rtc_err_2_str(rc), data_type, (unsigned)len);
        return false;
    }
    return true;
}

bool AgoraChannel::SendText(const std::string& text) {
    if (!connected_ || stream_id_ < 0) {
        return false;
    }
    // Agora data stream payload has a strict upper bound. For larger JSON
    // payloads (e.g. MCP tools/list response), split into chunks so the server
    // can reassemble with "<message_id>|<index>|<total>|<content>".
    constexpr size_t kMaxStreamMessageBytes = 1024;
    constexpr size_t kChunkPayloadBytes = 900;

    if (text.size() <= kMaxStreamMessageBytes) {
        int rc = agora_rtc_send_stream_message(conn_id_, stream_id_,
                                               text.c_str(), text.size());
        if (rc < 0) {
            ESP_LOGE(TAG, "agora_rtc_send_stream_message failed: %s", agora_rtc_err_2_str(rc));
            return false;
        }
        return true;
    }

    const std::string message_id = std::to_string((long long)esp_timer_get_time());
    const size_t total_parts = (text.size() + kChunkPayloadBytes - 1) / kChunkPayloadBytes;
    ESP_LOGW(TAG, "SendText too large (%u bytes), chunking into %u parts",
             (unsigned)text.size(), (unsigned)total_parts);

    size_t offset = 0;
    for (size_t i = 0; i < total_parts; ++i) {
        const size_t chunk_len = std::min(kChunkPayloadBytes, text.size() - offset);
        std::string chunk = message_id + "|" + std::to_string(i) + "|" +
                            std::to_string(total_parts) + "|" +
                            text.substr(offset, chunk_len);
        offset += chunk_len;

        int rc = agora_rtc_send_stream_message(conn_id_, stream_id_,
                                               chunk.c_str(), chunk.size());
        if (rc < 0) {
            ESP_LOGE(TAG, "agora_rtc_send_stream_message chunk %u/%u failed: %s",
                     (unsigned)(i + 1), (unsigned)total_parts, agora_rtc_err_2_str(rc));
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal event handlers
// ─────────────────────────────────────────────────────────────────────────────

void AgoraChannel::OnJoinSuccess(connection_id_t /*conn_id*/, uint32_t uid, int elapsed_ms) {
    ESP_LOGI(TAG, "Joined channel successfully (uid=%lu, elapsed=%d ms)",
             (unsigned long)uid, elapsed_ms);
    xEventGroupSetBits(event_group_, AGORA_CHANNEL_JOIN_SUCCESS_EVENT);
}

void AgoraChannel::OnConnectionLost(connection_id_t /*conn_id*/) {
    ESP_LOGW(TAG, "Agora connection lost (permanent)");
    connected_ = false;
    stream_id_ = -1;
    if (on_disconnected_) {
        on_disconnected_();
    }
}

void AgoraChannel::OnReconnecting(connection_id_t /*conn_id*/) {
    // Transient network interruption — SDK will attempt to rejoin automatically.
    // Pause audio/text sending until on_rejoin_channel_success fires.
    ESP_LOGW(TAG, "Agora reconnecting (transient network interruption)...");
    connected_ = false;
    stream_id_ = -1;
    // Do NOT call on_disconnected_: let the SDK handle the reconnection silently.
}

void AgoraChannel::OnRejoinSuccess(connection_id_t /*conn_id*/, uint32_t uid, int elapsed_ms) {
    ESP_LOGI(TAG, "Rejoin channel success (uid=%lu, elapsed=%d ms)",
             (unsigned long)uid, elapsed_ms);
    CreateDataStream();
    connected_ = true;
}

void AgoraChannel::OnAudioData(const void* data, size_t len,
                                const audio_frame_info_t* /*info*/) {
    if (on_data_) {
        on_data_(static_cast<const char*>(data), len, true);
    }
}

void AgoraChannel::OnStreamMessage(const char* data, size_t length) {
    if (on_data_) {
        on_data_(data, length, false);
    }
}

void AgoraChannel::OnError(int code, const char* msg) {
    // Error 130 = data stream reliability issue (missed/cached packets from peer).
    // This is non-fatal and typically happens when the peer's data stream message
    // arrives slightly out of order or is momentarily delayed. Do NOT disconnect.
    if (code == 130) {
        ESP_LOGW(TAG, "Agora non-fatal error %d (data stream): %s", code, msg ? msg : "");
        return;
    }
    ESP_LOGE(TAG, "Agora fatal error %d: %s", code, msg ? msg : "");
    connected_ = false;
    if (on_disconnected_) {
        on_disconnected_();
    }
}
