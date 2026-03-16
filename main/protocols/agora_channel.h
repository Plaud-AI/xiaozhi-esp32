#ifndef AGORA_CHANNEL_H
#define AGORA_CHANNEL_H

#include "channel.h"

#include <agora_rtc_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <string>

#define AGORA_CHANNEL_JOIN_SUCCESS_EVENT (1 << 0)
#define AGORA_CHANNEL_JOIN_TIMEOUT_MS    15000

// Agora WebRTC implementation of the Channel interface.
//
// The device only needs four credentials to join a channel:
//   app_id, channel, token, uid
// All other Agora / LLM / TTS / ASR configuration is managed server-side.
//
// On Connect():
//   1. Reads the four credentials from NVS namespace "agora".
//   2. Initialises the Agora RTSA SDK.
//   3. Joins the RTC channel and waits for on_join_channel_success.
//      (The AI Agent has already been started by the business server before
//       returning the credentials to the device.)
//
// Audio is sent via Agora's native audio channel using agora_rtc_send_audio_data()
// with pre-encoded OPUS frames (AUDIO_DATA_TYPE_OPUS, 16 kHz).
// SDK codec is DISABLED because we provide pre-encoded OPUS data directly.
// This gives us the benefits of Agora's native transport: FEC, jitter buffer,
// and proper RTP pacing — unlike data stream which has 1KB limits and no FEC.
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

    // Send audio via Agora's native audio channel (agora_rtc_send_audio_data).
    // data_type: AUDIO_DATA_TYPE_OPUS for pre-encoded OPUS,
    //            AUDIO_DATA_TYPE_PCM  for raw PCM (requires SDK codec enabled).
    bool SendNativeAudio(const void* data, size_t len, int data_type = 1 /*AUDIO_DATA_TYPE_OPUS*/);

    // Available after Connect() succeeds.
    const std::string& channel() const { return channel_; }

private:
    connection_id_t conn_id_   = CONNECTION_ID_INVALID;
    int             stream_id_ = -1;
    bool            connected_ = false;
    EventGroupHandle_t event_group_;

    // Credentials read from NVS "agora" namespace (set by server via OTA).
    std::string app_id_;
    std::string channel_;
    std::string token_;   // RTC Token generated server-side; empty = no auth
    uint32_t    uid_ = 0; // 0 = SDK auto-assigns

    bool ReadConfig();

    // Internal event handlers invoked from static C-callback bridges.
    void OnJoinSuccess(connection_id_t conn_id, uint32_t uid, int elapsed_ms);
    void OnConnectionLost(connection_id_t conn_id);
    void OnReconnecting(connection_id_t conn_id);
    void OnRejoinSuccess(connection_id_t conn_id, uint32_t uid, int elapsed_ms);
    void OnAudioData(const void* data, size_t len, const audio_frame_info_t* info);
    void OnStreamMessage(const char* data, size_t length);
    void OnError(int code, const char* msg);

    bool CreateDataStream();

    // Static singleton + C-callback bridges (Agora API uses raw function pointers).
    static AgoraChannel* s_instance_;
    static void S_OnJoinSuccess(connection_id_t, uint32_t, int);
    static void S_OnConnectionLost(connection_id_t);
    static void S_OnReconnecting(connection_id_t);
    static void S_OnRejoinSuccess(connection_id_t, uint32_t, int);
    static void S_OnAudioData(connection_id_t, uint32_t, uint16_t,
                              const void*, size_t, const audio_frame_info_t*);
    static void S_OnStreamMessage(connection_id_t, uint32_t, int,
                                  const char*, size_t, uint64_t);
    static void S_OnError(connection_id_t, int, const char*);
};

#endif // AGORA_CHANNEL_H
