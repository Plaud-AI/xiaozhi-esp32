#ifndef AGORA_CHANNEL_H
#define AGORA_CHANNEL_H

#include "channel.h"

#include <agora_rtc_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <string>
#include <memory>

#define AGORA_CHANNEL_JOIN_SUCCESS_EVENT (1 << 0)
#define AGORA_CHANNEL_JOIN_TIMEOUT_MS    15000

// Agora WebRTC implementation of the Channel interface.
//
// On Connect():
//   1. Reads configuration from NVS namespace "agora".
//   2. Initialises the Agora RTSA SDK.
//   3. Calls the Agora Conversational AI Agent REST API to start the cloud
//      agent in the same RTC channel.
//   4. Joins the Agora RTC channel and waits for on_join_channel_success.
//
// Audio is sent/received as raw OPUS frames (AUDIO_DATA_TYPE_OPUS, 16 kHz).
// JSON control messages travel over an Agora data stream.
class AgoraChannel : public Channel {
public:
    AgoraChannel();
    ~AgoraChannel() override;

    bool Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    bool SendText(const std::string& text) override;
    bool SendBinary(const void* data, size_t len) override;

    // Available after Connect() succeeds.
    const std::string& agent_id() const { return agent_id_; }

private:
    connection_id_t conn_id_   = CONNECTION_ID_INVALID;
    int             stream_id_ = -1;
    bool            connected_ = false;
    EventGroupHandle_t event_group_;

    // Configuration read from NVS "agora" namespace.
    std::string app_id_;
    std::string channel_;
    std::string token_;
    uint32_t    uid_        = 0;
    std::string agent_uid_; // agent's RTC UID, default "1001"
    std::string agent_name_;
    std::string api_key_;
    std::string api_secret_;

    // LLM / TTS / ASR config for the AI Agent join request.
    std::string llm_url_;
    std::string llm_key_;
    std::string llm_model_;
    std::string llm_system_;
    std::string llm_greeting_;
    std::string tts_vendor_;
    std::string tts_key_;
    std::string tts_region_;
    std::string tts_voice_;
    std::string asr_lang_;

    std::string agent_id_; // returned by AI Agent join response

    bool ReadConfig();
    bool CallAiAgentJoin();
    bool CallAiAgentLeave();
    std::string BuildBase64Credentials() const;

    // Internal event handlers called from static bridges.
    void OnJoinSuccess(connection_id_t conn_id, uint32_t uid, int elapsed_ms);
    void OnConnectionLost(connection_id_t conn_id);
    void OnAudioData(connection_id_t conn_id, uint32_t uid,
                     const void* data, size_t len,
                     const audio_frame_info_t* info);
    void OnStreamMessage(connection_id_t conn_id, uint32_t uid, int stream_id,
                         const char* data, size_t length);
    void OnError(connection_id_t conn_id, int code, const char* msg);

    // Static singleton + C callback bridges (Agora API uses raw function pointers).
    static AgoraChannel* s_instance_;
    static void S_OnJoinSuccess(connection_id_t, uint32_t, int);
    static void S_OnConnectionLost(connection_id_t);
    static void S_OnAudioData(connection_id_t, uint32_t, uint16_t,
                              const void*, size_t, const audio_frame_info_t*);
    static void S_OnStreamMessage(connection_id_t, uint32_t, int,
                                  const char*, size_t, uint64_t);
    static void S_OnError(connection_id_t, int, const char*);
};

#endif // AGORA_CHANNEL_H
