#ifndef AGORA_PROTOCOL_H
#define AGORA_PROTOCOL_H

#include "protocol.h"
#include "channel.h"

#include <memory>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

// Agora WebRTC implementation of the Protocol interface.
//
// Differs from WebsocketProtocol in that:
//  - Audio is sent via Agora's native audio channel (agora_rtc_send_audio_data)
//    as pre-encoded OPUS frames (AUDIO_DATA_TYPE_OPUS). This provides FEC,
//    jitter buffer, and proper RTP pacing — unlike data stream.
//  - JSON control messages travel over an Agora RTC data stream.
//  - During TTS playback the device sends OPUS silence frames (half-duplex)
//    to prevent echo from reaching the server's VAD/STT.
//  - server_sample_rate_ is fixed at 16000; server_frame_duration_ at 60 ms,
//    matching the xiaozhi OPUS pipeline.
class AgoraProtocol : public Protocol {
public:
    AgoraProtocol();
    ~AgoraProtocol() override;

    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

    // Called by Application after the audio decode/playback queue is empty.
    void NotifyPlaybackComplete();

private:
    std::unique_ptr<Channel> channel_;
    int audio_packets_sent_ = 0;

    // Continuous silence sender: keeps the RTC audio stream alive with
    // DTX silence in two scenarios:
    //  1. During TTS playback (tts_playing_ = true) — prevents echo.
    //  2. During listening when AFE gate is closed — fills the audio gap
    //     so the server's Deepgram ASR can properly endpoint utterances.
    // The timer runs for the entire lifetime of the audio channel.
    std::vector<uint8_t> opus_silence_frame_;
    TimerHandle_t silence_timer_ = nullptr;
    bool tts_playing_ = false;
    int64_t last_audio_send_us_ = 0;   // esp_timer_get_time() of last real audio or TTS-end
    uint32_t gap_dtx_count_ = 0;

    void StartSilenceSender();
    void StopSilenceSender();
    static void SilenceTimerCallback(TimerHandle_t timer);

    bool SendText(const std::string& text) override;
    bool SendHello();
    void HandleIncomingData(const char* data, size_t len, bool binary);

    // tts:stop has been received but we are still waiting for the local
    // audio playback queue to drain before clearing tts_playing_.
    bool tts_stop_pending_ = false;
};

#endif // AGORA_PROTOCOL_H
