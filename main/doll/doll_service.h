#ifndef DOLL_SERVICE_H
#define DOLL_SERVICE_H

#include <string>
#include <functional>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION

class NfcReader;
class PressureSensor;
class TouchSensor;

// 触摸位置枚举
enum class TouchPosition {
    kTouchNone = 0,
    kTouchTop,
    kTouchLeft,
    kTouchRight,
    kTouchBottom,
};

// 手办事件回调
struct DollEventCallbacks {
    std::function<void(const std::string& doll_id)> on_doll_placed;
    std::function<void()> on_doll_removed;
    std::function<void(TouchPosition pos)> on_touched;
};

/**
 * @brief 手办服务 - 负责手办硬件交互
 * 
 * 职责：
 * - NFC 标签读取
 * - 压力传感器检测
 * - 触摸传感器检测
 * - 事件通知
 */
class DollService {
public:
    static DollService& GetInstance();

    // 初始化与控制
    void Initialize();
    void Start();
    void Stop();
    bool IsRunning() const { return running_; }

    // 手办检测
    bool IsDollPresent() const;
    std::string GetCurrentDollId() const;
    
    // 触摸检测
    bool IsTouched() const;
    TouchPosition GetTouchPosition() const;

    // 事件回调设置
    void SetEventCallbacks(const DollEventCallbacks& callbacks);

    // 手动触发（用于测试和 MCP 工具）
    void SimulateDollPlaced(const std::string& doll_id);
    void SimulateDollRemoved();
    void SimulateTouch(TouchPosition pos);

    // 重新读取 NFC（用于失败重试）
    bool RetryNfcRead();

private:
    DollService();
    ~DollService();
    DollService(const DollService&) = delete;
    DollService& operator=(const DollService&) = delete;

    static void DetectionTask(void* param);
    void RunDetectionLoop();
    void HandleDollPlaced(const std::string& doll_id);
    void HandleDollRemoved();
    void HandleTouched(TouchPosition pos);

    bool running_;
    std::string current_doll_id_;
    bool doll_present_;
    TouchPosition current_touch_;
    
    std::unique_ptr<NfcReader> nfc_reader_;
    std::unique_ptr<PressureSensor> pressure_sensor_;
    std::unique_ptr<TouchSensor> touch_sensor_;
    
    DollEventCallbacks callbacks_;
    TaskHandle_t detection_task_handle_;
};

#endif // CONFIG_ENABLE_DOLL_INTERACTION

#endif // DOLL_SERVICE_H

