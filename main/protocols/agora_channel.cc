#include "agora_channel.h"
#include "settings.h"
#include "board.h"

#include <cstring>
#include <cstdio>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <mbedtls/base64.h>

#define TAG "AgoraChannel"

// Agora Conversational AI Agent v2 REST endpoint.
#define AGORA_AI_AGENT_API_URL \
    "https://api.agora.io/api/conversational-ai-agent/v2/projects"

// Timeouts
#define HTTP_TIMEOUT_MS  10000
#define MAX_HTTP_BUF     4096

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

void AgoraChannel::S_OnAudioData(connection_id_t conn_id, uint32_t uid,
                                  uint16_t /*sent_ts*/,
                                  const void* data, size_t len,
                                  const audio_frame_info_t* info) {
    if (s_instance_) s_instance_->OnAudioData(conn_id, uid, data, len, info);
}

void AgoraChannel::S_OnStreamMessage(connection_id_t conn_id, uint32_t uid,
                                      int stream_id, const char* data,
                                      size_t length, uint64_t /*sent_ts*/) {
    if (s_instance_) s_instance_->OnStreamMessage(conn_id, uid, stream_id, data, length);
}

void AgoraChannel::S_OnError(connection_id_t conn_id, int code, const char* msg) {
    if (s_instance_) s_instance_->OnError(conn_id, code, msg);
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
// Config
// ─────────────────────────────────────────────────────────────────────────────

bool AgoraChannel::ReadConfig() {
    Settings s("agora", false);
    app_id_      = s.GetString("app_id");
    channel_     = s.GetString("channel");
    token_       = s.GetString("token");
    uid_         = (uint32_t)s.GetInt("uid", 0);
    agent_uid_   = s.GetString("agent_uid", "1001");
    agent_name_  = s.GetString("agent_name", "xiaozhi");
    api_key_     = s.GetString("api_key");
    api_secret_  = s.GetString("api_secret");
    llm_url_     = s.GetString("llm_url");
    llm_key_     = s.GetString("llm_key");
    llm_model_   = s.GetString("llm_model", "gpt-4o-mini");
    llm_system_  = s.GetString("llm_system", "You are a helpful voice assistant.");
    llm_greeting_ = s.GetString("llm_greeting", "Hello, how can I help you?");
    tts_vendor_  = s.GetString("tts_vendor", "microsoft");
    tts_key_     = s.GetString("tts_key");
    tts_region_  = s.GetString("tts_region");
    tts_voice_   = s.GetString("tts_voice");
    asr_lang_    = s.GetString("asr_lang", "zh-CN");

    if (app_id_.empty()) {
        ESP_LOGE(TAG, "agora.app_id not configured in NVS");
        return false;
    }
    if (channel_.empty()) {
        ESP_LOGE(TAG, "agora.channel not configured in NVS");
        return false;
    }
    if (api_key_.empty() || api_secret_.empty()) {
        ESP_LOGE(TAG, "agora.api_key / api_secret not configured in NVS");
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Base64 helper (for HTTP Basic auth)
// ─────────────────────────────────────────────────────────────────────────────

std::string AgoraChannel::BuildBase64Credentials() const {
    std::string creds = api_key_ + ":" + api_secret_;
    size_t out_len = 0;
    // mbedtls_base64_encode with output=NULL returns required length
    mbedtls_base64_encode(nullptr, 0, &out_len,
                          (const unsigned char*)creds.data(), creds.size());
    std::string result(out_len, '\0');
    mbedtls_base64_encode((unsigned char*)result.data(), out_len, &out_len,
                          (const unsigned char*)creds.data(), creds.size());
    // mbedtls appends a '\0' counted in out_len; trim it
    while (!result.empty() && result.back() == '\0') result.pop_back();
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTP helper shared by Join and Leave calls
// ─────────────────────────────────────────────────────────────────────────────

struct HttpContext {
    char  buf[MAX_HTTP_BUF];
    int   len = 0;
    int   status_code = 0;
};

static esp_err_t http_event_handler(esp_http_client_event_t* evt) {
    auto* ctx = static_cast<HttpContext*>(evt->user_data);
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client)) {
            int copy = std::min((int)evt->data_len,
                                (int)(sizeof(ctx->buf) - ctx->len - 1));
            if (copy > 0) {
                memcpy(ctx->buf + ctx->len, evt->data, copy);
                ctx->len += copy;
                ctx->buf[ctx->len] = '\0';
            }
        }
        break;
    default:
        break;
    }
    return ESP_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// AI Agent REST API – Join
// ─────────────────────────────────────────────────────────────────────────────

bool AgoraChannel::CallAiAgentJoin() {
    // Build the request JSON.
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", agent_name_.c_str());

    cJSON* props = cJSON_CreateObject();
    cJSON_AddStringToObject(props, "channel", channel_.c_str());
    if (!token_.empty()) {
        cJSON_AddStringToObject(props, "token", token_.c_str());
    }
    cJSON_AddStringToObject(props, "agent_rtc_uid", agent_uid_.c_str());

    cJSON* remote_uids = cJSON_CreateArray();
    char uid_str[16];
    snprintf(uid_str, sizeof(uid_str), "%lu", (unsigned long)uid_);
    cJSON_AddItemToArray(remote_uids, cJSON_CreateString(uid_str));
    cJSON_AddItemToObject(props, "remote_rtc_uids", remote_uids);

    cJSON* params = cJSON_CreateObject();
    // Use OPUS output so AgoraProtocol can forward frames directly to the
    // existing OPUS audio pipeline without re-encoding.
    cJSON_AddStringToObject(params, "output_audio_codec", "OPUS");
    cJSON_AddItemToObject(props, "parameters", params);

    cJSON_AddNumberToObject(props, "idle_timeout", 120);

    cJSON* features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "enable_aivad", true);
    cJSON_AddItemToObject(props, "advanced_features", features);

    if (!llm_url_.empty()) {
        cJSON* llm = cJSON_CreateObject();
        cJSON_AddStringToObject(llm, "url", llm_url_.c_str());
        if (!llm_key_.empty()) {
            cJSON_AddStringToObject(llm, "api_key", llm_key_.c_str());
        }
        cJSON* sys_msgs = cJSON_CreateArray();
        cJSON* sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", llm_system_.c_str());
        cJSON_AddItemToArray(sys_msgs, sys_msg);
        cJSON_AddItemToObject(llm, "system_messages", sys_msgs);
        cJSON_AddNumberToObject(llm, "max_history", 32);
        cJSON_AddStringToObject(llm, "greeting_message", llm_greeting_.c_str());
        cJSON* llm_params = cJSON_CreateObject();
        cJSON_AddStringToObject(llm_params, "model", llm_model_.c_str());
        cJSON_AddItemToObject(llm, "params", llm_params);
        cJSON_AddItemToObject(props, "llm", llm);
    }

    if (!tts_vendor_.empty()) {
        cJSON* tts = cJSON_CreateObject();
        cJSON_AddStringToObject(tts, "vendor", tts_vendor_.c_str());
        cJSON* tts_params = cJSON_CreateObject();
        if (!tts_key_.empty())    cJSON_AddStringToObject(tts_params, "key",        tts_key_.c_str());
        if (!tts_region_.empty()) cJSON_AddStringToObject(tts_params, "region",     tts_region_.c_str());
        if (!tts_voice_.empty())  cJSON_AddStringToObject(tts_params, "voice_name", tts_voice_.c_str());
        cJSON_AddItemToObject(tts, "params", tts_params);
        cJSON_AddItemToObject(props, "tts", tts);
    }

    cJSON* asr = cJSON_CreateObject();
    cJSON_AddStringToObject(asr, "language", asr_lang_.c_str());
    cJSON_AddItemToObject(props, "asr", asr);

    cJSON_AddItemToObject(root, "properties", props);

    char* body_raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body_raw) {
        ESP_LOGE(TAG, "Failed to build AI Agent join JSON");
        return false;
    }
    std::string body(body_raw);
    cJSON_free(body_raw);

    // Build URL and auth header.
    char url[256];
    snprintf(url, sizeof(url), "%s/%s/join", AGORA_AI_AGENT_API_URL, app_id_.c_str());
    std::string auth = "Basic " + BuildBase64Credentials();

    HttpContext ctx{};
    esp_http_client_config_t cfg{};
    cfg.url               = url;
    cfg.event_handler     = http_event_handler;
    cfg.user_data         = &ctx;
    cfg.timeout_ms        = HTTP_TIMEOUT_MS;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    auto client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for AI Agent join");
        return false;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type",  "application/json");
    esp_http_client_set_header(client, "Authorization", auth.c_str());

    bool ok = false;
    esp_err_t err = esp_http_client_open(client, (int)body.size());
    if (err == ESP_OK) {
        esp_http_client_write(client, body.c_str(), (int)body.size());
        esp_http_client_fetch_headers(client);
        ctx.status_code = esp_http_client_get_status_code(client);
        // Drain response body (populated via HTTP_EVENT_ON_DATA above).
        esp_http_client_read_response(client, ctx.buf + ctx.len,
                                      sizeof(ctx.buf) - ctx.len - 1);
        if (ctx.status_code == 200) {
            // Parse agent_id from response.
            cJSON* resp = cJSON_Parse(ctx.buf);
            if (resp) {
                auto* id = cJSON_GetObjectItemCaseSensitive(resp, "agent_id");
                if (cJSON_IsString(id) && id->valuestring) {
                    agent_id_ = id->valuestring;
                    ESP_LOGI(TAG, "AI Agent started, agent_id=%s", agent_id_.c_str());
                }
                cJSON_Delete(resp);
            }
            ok = true;
        } else {
            ESP_LOGE(TAG, "AI Agent join failed, HTTP %d: %s",
                     ctx.status_code, ctx.buf);
        }
    } else {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// AI Agent REST API – Leave
// ─────────────────────────────────────────────────────────────────────────────

bool AgoraChannel::CallAiAgentLeave() {
    if (agent_id_.empty()) {
        ESP_LOGW(TAG, "No agent_id; skipping AI Agent leave call");
        return true;
    }
    char url[320];
    snprintf(url, sizeof(url), "%s/%s/agents/%s/leave",
             AGORA_AI_AGENT_API_URL, app_id_.c_str(), agent_id_.c_str());
    std::string auth = "Basic " + BuildBase64Credentials();

    HttpContext ctx{};
    esp_http_client_config_t cfg{};
    cfg.url               = url;
    cfg.event_handler     = http_event_handler;
    cfg.user_data         = &ctx;
    cfg.timeout_ms        = HTTP_TIMEOUT_MS;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    auto client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for AI Agent leave");
        return false;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Authorization", auth.c_str());

    bool ok = false;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ctx.status_code = esp_http_client_get_status_code(client);
        ok = (ctx.status_code == 200);
        if (ok) {
            ESP_LOGI(TAG, "AI Agent stopped (agent_id=%s)", agent_id_.c_str());
            agent_id_.clear();
        } else {
            ESP_LOGE(TAG, "AI Agent leave failed, HTTP %d", ctx.status_code);
        }
    } else {
        ESP_LOGE(TAG, "AI Agent leave HTTP error: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return ok;
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
    handler.on_join_channel_success = S_OnJoinSuccess;
    handler.on_connection_lost      = S_OnConnectionLost;
    handler.on_audio_data           = S_OnAudioData;
    handler.on_stream_message       = S_OnStreamMessage;
    handler.on_error                = S_OnError;

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

    // Start the AI Agent in the cloud BEFORE joining the channel ourselves
    // so the agent has time to join.
    if (!CallAiAgentJoin()) {
        ESP_LOGE(TAG, "AI Agent join failed; aborting RTC connection");
        agora_rtc_destroy_connection(conn_id_);
        agora_rtc_fini();
        conn_id_ = CONNECTION_ID_INVALID;
        return false;
    }

    // Join the RTC channel.  Disable built-in codec so we can send pre-encoded
    // OPUS frames directly via AUDIO_DATA_TYPE_OPUS.
    rtc_channel_options_t ch_opts{};
    ch_opts.auto_subscribe_audio            = true;
    ch_opts.auto_subscribe_video            = false;
    ch_opts.audio_codec_opt.audio_codec_type = AUDIO_CODEC_DISABLED;

    const char* token_ptr = token_.empty() ? nullptr : token_.c_str();
    rc = agora_rtc_join_channel(conn_id_, channel_.c_str(), uid_, token_ptr, &ch_opts);
    if (rc < 0) {
        ESP_LOGE(TAG, "agora_rtc_join_channel failed: %s", agora_rtc_err_2_str(rc));
        CallAiAgentLeave();
        agora_rtc_destroy_connection(conn_id_);
        agora_rtc_fini();
        conn_id_ = CONNECTION_ID_INVALID;
        return false;
    }

    ESP_LOGI(TAG, "Waiting for join_channel_success (channel=%s)...", channel_.c_str());
    EventBits_t bits = xEventGroupWaitBits(event_group_,
                                           AGORA_CHANNEL_JOIN_SUCCESS_EVENT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(AGORA_CHANNEL_JOIN_TIMEOUT_MS));
    if (!(bits & AGORA_CHANNEL_JOIN_SUCCESS_EVENT)) {
        ESP_LOGE(TAG, "Timed out waiting for join_channel_success");
        CallAiAgentLeave();
        agora_rtc_leave_channel(conn_id_);
        agora_rtc_destroy_connection(conn_id_);
        agora_rtc_fini();
        conn_id_ = CONNECTION_ID_INVALID;
        return false;
    }

    // Create an Agora data stream for bidirectional JSON control messages.
    rc = agora_rtc_create_data_stream(conn_id_, &stream_id_, false, false);
    if (rc < 0) {
        ESP_LOGW(TAG, "Failed to create data stream: %s (JSON messages will be unavailable)",
                 agora_rtc_err_2_str(rc));
        stream_id_ = -1;
    }

    connected_ = true;
    ESP_LOGI(TAG, "Agora channel connected (channel=%s, agent_id=%s)",
             channel_.c_str(), agent_id_.c_str());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Channel::Disconnect
// ─────────────────────────────────────────────────────────────────────────────

void AgoraChannel::Disconnect() {
    if (!connected_ && conn_id_ == CONNECTION_ID_INVALID) {
        return;
    }
    connected_ = false;

    CallAiAgentLeave();

    if (conn_id_ != CONNECTION_ID_INVALID) {
        agora_rtc_leave_channel(conn_id_);
        agora_rtc_destroy_connection(conn_id_);
        conn_id_   = CONNECTION_ID_INVALID;
        stream_id_ = -1;
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
    info.data_type = AUDIO_DATA_TYPE_OPUS; // 16 kHz OPUS frames
    int rc = agora_rtc_send_audio_data(conn_id_, data, len, &info);
    if (rc < 0) {
        ESP_LOGE(TAG, "agora_rtc_send_audio_data failed: %s", agora_rtc_err_2_str(rc));
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
    ESP_LOGI(TAG, "Joined Agora channel successfully (uid=%lu, elapsed=%d ms)",
             (unsigned long)uid, elapsed_ms);
    xEventGroupSetBits(event_group_, AGORA_CHANNEL_JOIN_SUCCESS_EVENT);
}

void AgoraChannel::OnConnectionLost(connection_id_t /*conn_id*/) {
    ESP_LOGW(TAG, "Agora connection lost");
    connected_ = false;
    if (on_disconnected_) {
        on_disconnected_();
    }
}

void AgoraChannel::OnAudioData(connection_id_t /*conn_id*/, uint32_t /*uid*/,
                                const void* data, size_t len,
                                const audio_frame_info_t* /*info*/) {
    // Forward incoming audio (OPUS frames from AI agent) to the protocol layer.
    if (on_data_) {
        on_data_(static_cast<const char*>(data), len, true);
    }
}

void AgoraChannel::OnStreamMessage(connection_id_t /*conn_id*/, uint32_t /*uid*/,
                                    int /*stream_id*/, const char* data, size_t length) {
    // Forward incoming JSON messages from the AI agent to the protocol layer.
    if (on_data_) {
        on_data_(data, length, false);
    }
}

void AgoraChannel::OnError(connection_id_t /*conn_id*/, int code, const char* msg) {
    ESP_LOGE(TAG, "Agora error %d: %s", code, msg ? msg : "");
    connected_ = false;
    if (on_disconnected_) {
        on_disconnected_();
    }
}
