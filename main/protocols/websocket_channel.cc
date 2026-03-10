#include "websocket_channel.h"
#include "board.h"
#include "system_info.h"
#include "settings.h"

#include <esp_log.h>

#define TAG "WSChannel"

// Official OTA server identifiers used to select compatible protocol headers.
#define OFFICIAL_OTA_DOMAIN   "api.tenclass.net"
#define OFFICIAL_OTA_DOMAIN_2 "2662r3426b.vicp.fun"
#define OFFICIAL_OTA_IP       "44.228.155.146"

WebsocketChannel::WebsocketChannel() = default;

bool WebsocketChannel::IsOfficialServer(const std::string& ota_url) const {
    return ota_url.find(OFFICIAL_OTA_DOMAIN) != std::string::npos ||
           ota_url.find(OFFICIAL_OTA_DOMAIN_2) != std::string::npos ||
           ota_url.find(OFFICIAL_OTA_IP) != std::string::npos;
}

bool WebsocketChannel::Connect() {
    // Read connection parameters from NVS.
    Settings settings("websocket", false);
    std::string url   = settings.GetString("url");
    std::string token = settings.GetString("token");
    int version       = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    if (url.empty()) {
        ESP_LOGE(TAG, "WebSocket URL not configured in NVS, please configure OTA server first");
        return false;
    }

    // Determine whether we are talking to the official server so we can
    // choose the correct Device-Id / Client-Id header values.
    std::string ota_url;
    Settings system_settings("system", false);
    ota_url = system_settings.GetString("ota_url", "");
    if (ota_url.empty()) {
        Settings wifi_settings("wifi", false);
        ota_url = wifi_settings.GetString("ota_url", CONFIG_OTA_URL);
    }
    is_official_server_ = IsOfficialServer(ota_url);

    ESP_LOGI(TAG, "WebSocket configuration - URL: %s, Version: %d, Official: %s",
             url.c_str(), version_, is_official_server_ ? "Yes" : "No");

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    // Set authentication header.
    if (!token.empty()) {
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());

    // Official and custom servers use different identity headers.
    if (is_official_server_) {
        websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
        websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
        ESP_LOGI(TAG, "Using official server headers: Device-Id=MAC, Client-Id=UUID");
    } else {
        websocket_->SetHeader("Device-Id", Board::GetInstance().GetDeviceId().c_str());
        websocket_->SetHeader("Client-Id", Board::GetInstance().GetDeviceId().c_str());
        ESP_LOGI(TAG, "Using custom server headers: Device-Id=DeviceId, Client-Id=DeviceId");
    }

    // Forward raw WebSocket events to the Channel callbacks.
    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (on_data_) {
            on_data_(data, len, binary);
        }
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "WebSocket disconnected");
        if (on_disconnected_) {
            on_disconnected_();
        }
    });

    ESP_LOGI(TAG, "Connecting to WebSocket server: %s (protocol v%d)", url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to WebSocket server: %s", url.c_str());
        websocket_.reset();
        return false;
    }

    ESP_LOGI(TAG, "WebSocket connection established: %s", url.c_str());
    return true;
}

void WebsocketChannel::Disconnect() {
    websocket_.reset();
}

bool WebsocketChannel::IsConnected() const {
    return websocket_ != nullptr && websocket_->IsConnected();
}

bool WebsocketChannel::SendText(const std::string& text) {
    if (!IsConnected()) {
        return false;
    }
    return websocket_->Send(text);
}

bool WebsocketChannel::SendBinary(const void* data, size_t len) {
    if (!IsConnected()) {
        return false;
    }
    return websocket_->Send(static_cast<const char*>(data), len, true);
}
