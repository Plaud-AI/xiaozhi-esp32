#ifndef WEBSOCKET_CHANNEL_H
#define WEBSOCKET_CHANNEL_H

#include "channel.h"

#include <web_socket.h>
#include <string>
#include <memory>

// WebSocket implementation of the Channel interface.
// Reads connection parameters from NVS (namespace "websocket": url, token, version),
// determines whether the target is an official server, sets the required HTTP headers,
// and manages the underlying WebSocket object's lifetime.
class WebsocketChannel : public Channel {
public:
    WebsocketChannel();
    ~WebsocketChannel() override = default;

    bool Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    bool SendText(const std::string& text) override;
    bool SendBinary(const void* data, size_t len) override;

    // Available after a successful Connect().
    int version() const { return version_; }
    bool is_official_server() const { return is_official_server_; }

private:
    std::unique_ptr<WebSocket> websocket_;
    int version_ = 1;
    bool is_official_server_ = true;

    bool IsOfficialServer(const std::string& ota_url) const;
};

#endif // WEBSOCKET_CHANNEL_H
