#ifndef BOARD_H
#define BOARD_H

#include <http.h>
#include <web_socket.h>
#include <mqtt.h>
#include <udp.h>
#include <string>
#include <network_interface.h>

#include "led/led.h"
#include "backlight.h"
#include "camera.h"
#include "assets.h"


void* create_board();
class AudioCodec;
class Display;
class CsPressDriver;
class NfcDriver;
class DualServoController;
class Ws2812Driver;

class Board {
private:
    Board(const Board&) = delete; // 禁用拷贝构造函数
    Board& operator=(const Board&) = delete; // 禁用赋值操作

protected:
    Board();
    
    /**
     * @brief 生成设备唯一标识（基于 eFuse MAC）
     * @return 格式: "XZA000-XXXXXXXXXXXX"
     */
    std::string GenerateDeviceId();
    
    /**
     * @brief 生成标准 UUID v4（用于官方服务器兼容）
     * @return 格式: "xxxxxxxx-xxxx-4xxx-xxxx-xxxxxxxxxxxx"
     */
    std::string GenerateUuid();

    // 基于硬件的设备唯一标识（永不变化）
    std::string device_id_;
    
    // 标准 UUID v4（用于官方服务器兼容，存储在 NVS）
    std::string uuid_;

public:
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    virtual ~Board() = default;
    virtual std::string GetBoardType() = 0;
    
    /**
     * @brief 获取设备唯一标识
     * @return 格式: "XZA000-XXXXXXXXXXXX"（基于 eFuse MAC，永不变化）
     */
    virtual std::string GetDeviceId() { return device_id_; }
    
    /**
     * @brief 获取标准 UUID v4（用于官方服务器兼容）
     * @return 格式: "xxxxxxxx-xxxx-4xxx-xxxx-xxxxxxxxxxxx"
     */
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight* GetBacklight() { return nullptr; }
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual bool GetTemperature(float& esp32temp);
    virtual Display* GetDisplay();
    virtual Camera* GetCamera();
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void StartNetwork() = 0;
    virtual const char* GetNetworkStateIcon() = 0;
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging);
    virtual std::string GetSystemInfoJson();
    virtual void SetPowerSaveMode(bool enabled) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
    virtual CsPressDriver* GetPressureDriver() { return nullptr; }
    virtual NfcDriver* GetNfcDriver() { return nullptr; }
    virtual DualServoController* GetDualServoController() { return nullptr; }
    virtual Ws2812Driver* GetWs2812Driver() { return nullptr; }
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#endif // BOARD_H
