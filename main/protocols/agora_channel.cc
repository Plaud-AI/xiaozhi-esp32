#include "agora_channel.h"
#include "settings.h"

#include <esp_log.h>

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

    // The prebuilt Agora SDK (v1.9.5) does NOT include a built-in OPUS encoder
    // (attempting AUDIO_CODEC_TYPE_OPUS causes abort in audio_stream_init).
    // We disable the SDK codec and send pre-encoded OPUS frames directly.
    rtc_channel_options_t ch_opts{};
    ch_opts.auto_subscribe_audio             = true;
    ch_opts.auto_subscribe_video             = false;
    ch_opts.audio_codec_opt.audio_codec_type = AUDIO_CODEC_DISABLED;

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
    int rc = agora_rtc_create_data_stream(conn_id_, &stream_id_, false, false);
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
    if (!connected_ || conn_id_ == CONNECTION_ID_INVALID) {
        return false;
    }
    audio_frame_info_t info{};
    info.data_type = AUDIO_DATA_TYPE_OPUS;
    int rc = agora_rtc_send_audio_data(conn_id_, data, len, &info);
    if (rc < 0) {
        ESP_LOGW(TAG, "agora_rtc_send_audio_data failed: %s", agora_rtc_err_2_str(rc));
        return false;
    }
    return true;
}

bool AgoraChannel::SendText(const std::string& text) {
    if (!connected_ || stream_id_ < 0) {
        return false;
    }
    int rc = agora_rtc_send_stream_message(conn_id_, stream_id_,
                                           text.c_str(), text.size());
    if (rc < 0) {
        ESP_LOGE(TAG, "agora_rtc_send_stream_message failed: %s", agora_rtc_err_2_str(rc));
        return false;
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
