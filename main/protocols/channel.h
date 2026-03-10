#ifndef CHANNEL_H
#define CHANNEL_H

#include <string>
#include <functional>

// Abstract transport channel interface.
// Implementations handle the physical connection (WebSocket, WebRTC, etc.)
// while Protocol subclasses handle application-level framing and business logic.
class Channel {
public:
    virtual ~Channel() = default;

    // Establish the transport connection. Returns true on success.
    virtual bool Connect() = 0;

    // Tear down the transport connection.
    virtual void Disconnect() = 0;

    // Returns true when the transport is currently connected.
    virtual bool IsConnected() const = 0;

    // Send a UTF-8 text frame.
    virtual bool SendText(const std::string& text) = 0;

    // Send a binary frame.
    virtual bool SendBinary(const void* data, size_t len) = 0;

    // Register a callback for incoming data.
    // |binary| is true for binary frames, false for text frames.
    void OnData(std::function<void(const char* data, size_t len, bool binary)> callback) {
        on_data_ = std::move(callback);
    }

    // Register a callback invoked when the transport disconnects unexpectedly.
    void OnDisconnected(std::function<void()> callback) {
        on_disconnected_ = std::move(callback);
    }

protected:
    std::function<void(const char* data, size_t len, bool binary)> on_data_;
    std::function<void()> on_disconnected_;
};

#endif // CHANNEL_H
