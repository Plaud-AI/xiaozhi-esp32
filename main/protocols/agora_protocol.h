#ifndef AGORA_PROTOCOL_H
#define AGORA_PROTOCOL_H

#include "protocol.h"
#include "channel.h"

#include <memory>

// Agora WebRTC implementation of the Protocol interface.
//
// Differs from WebsocketProtocol in that:
//  - There is no hello / server-hello handshake; the Agora AI Agent manages
//    conversation flow automatically.
//  - Audio is sent/received as raw OPUS frames (16 kHz, no BinaryProtocol
//    wrapping).  SDK codec is disabled (prebuilt SDK lacks OPUS encoder).
//  - JSON control messages travel over an Agora RTC data stream.
//  - server_sample_rate_ is fixed at 16000; server_frame_duration_ at 60 ms,
//    matching the xiaozhi OPUS pipeline.
class AgoraProtocol : public Protocol {
public:
    AgoraProtocol();
    ~AgoraProtocol() override = default;

    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

private:
    std::unique_ptr<Channel> channel_;
    int audio_packets_sent_ = 0;

    bool SendText(const std::string& text) override;
    void HandleIncomingData(const char* data, size_t len, bool binary);
};

#endif // AGORA_PROTOCOL_H
