#ifndef _HARDWARE_TEST_SERVICE_H_
#define _HARDWARE_TEST_SERVICE_H_

#include <esp_err.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cJSON.h>

// 前向声明
class ServoDriver;
class NfcDriver;

/**
 * @brief 测试模块状态结构体
 */
struct TestModuleStatus {
    bool initialized;
    std::string error_message;
    cJSON* extra_info;  // 额外信息，调用者负责释放

    TestModuleStatus() : initialized(false), extra_info(nullptr) {}
    ~TestModuleStatus() {
        if (extra_info) {
            cJSON_Delete(extra_info);
        }
    }
};

/**
 * @brief 自检结果结构体
 */
struct SelfCheckResult {
    bool passed;
    std::string message;
};

/**
 * @brief LED 颜色结构体
 */
struct LedColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    
    LedColor() : r(0), g(0), b(0) {}
    LedColor(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
};

/**
 * @brief 表情信息结构体
 */
struct EmotionInfo {
    std::string name;
    std::string display;
};

/**
 * @brief 硬件测试服务类
 * 
 * 统一封装硬件测试功能，提供给 BLE 指令和其他入口使用。
 * 采用单例模式，所有硬件驱动的生命周期由此服务管理。
 * 
 * 分层设计：
 * ┌─────────────────────────────────────────┐
 * │  BLE 指令层 / MCP 工具层 / 其他入口    │
 * ├─────────────────────────────────────────┤
 * │        HardwareTestService             │ ← 测试服务层
 * ├─────────────────────────────────────────┤
 * │ ServoDriver │ NfcDriver │ Led │ Display│ ← 硬件驱动层
 * └─────────────────────────────────────────┘
 */
class HardwareTestService {
public:
    /**
     * @brief 获取单例实例
     */
    static HardwareTestService& GetInstance();

    // ═══════════════════════════════════════════════════════════════
    // 舵机测试接口
    // ═══════════════════════════════════════════════════════════════
    
    /**
     * @brief 初始化舵机
     * @param gpio_pin GPIO 引脚（-1 使用默认配置）
     * @return JSON 响应字符串
     */
    std::string ServoInit(int gpio_pin = -1);

    /**
     * @brief 设置舵机角度
     * @param angle 角度 (0-180)
     * @return JSON 响应字符串
     */
    std::string ServoSetAngle(uint32_t angle);

    /**
     * @brief 获取舵机角度
     * @return JSON 响应字符串
     */
    std::string ServoGetAngle();

    /**
     * @brief 舵机相对移动
     * @param angle 移动角度
     * @param direction 方向 ("forward" 或 "reverse")
     * @return JSON 响应字符串
     */
    std::string ServoMove(uint32_t angle, const std::string& direction);

    /**
     * @brief 舵机扫描测试
     * @param min_angle 最小角度
     * @param max_angle 最大角度
     * @param speed 每步角度
     * @param cycles 往返次数
     * @return JSON 响应字符串
     */
    std::string ServoSweep(uint32_t min_angle, uint32_t max_angle, 
                           uint32_t speed, uint32_t cycles);

    // ═══════════════════════════════════════════════════════════════
    // NFC 测试接口
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 初始化 NFC
     * @param uart_port UART 端口 (-1 使用默认配置)
     * @param tx_pin TX 引脚 (-1 使用默认配置)
     * @param rx_pin RX 引脚 (-1 使用默认配置)
     * @return JSON 响应字符串
     */
    std::string NfcInit(int uart_port = -1, int tx_pin = -1, int rx_pin = -1);

    /**
     * @brief NFC 单次轮询
     * @return JSON 响应字符串
     */
    std::string NfcPoll();

    /**
     * @brief 启动/停止 NFC 连续模式
     * @param enable 是否启用
     * @param interval_ms 轮询间隔
     * @return JSON 响应字符串
     */
    std::string NfcContinuous(bool enable, uint32_t interval_ms = 200);

    /**
     * @brief 释放 NFC 资源
     * @return JSON 响应字符串
     */
    std::string NfcDeinit();

    /**
     * @brief 设置 NFC 卡片事件回调
     * @param callback 回调函数，参数为 JSON 格式的事件数据
     */
    void SetNfcEventCallback(std::function<void(const std::string&)> callback);

    // ═══════════════════════════════════════════════════════════════
    // LED 测试接口
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 设置 LED 颜色
     * @param r 红色 (0-255)
     * @param g 绿色 (0-255)
     * @param b 蓝色 (0-255)
     * @return JSON 响应字符串
     */
    std::string LedSetColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief 设置 LED 亮度
     * @param brightness 亮度 (0-100)
     * @return JSON 响应字符串
     */
    std::string LedSetBrightness(uint8_t brightness);

    /**
     * @brief 播放 LED 灯效
     * @param effect 灯效名称
     * @param duration_ms 持续时间
     * @param loop 是否循环
     * @return JSON 响应字符串
     */
    std::string LedEffect(const std::string& effect, uint32_t duration_ms, bool loop = false);

    /**
     * @brief 停止 LED 灯效
     * @return JSON 响应字符串
     */
    std::string LedStop();

    // ═══════════════════════════════════════════════════════════════
    // 表情测试接口
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 播放表情动画
     * @param emotion 表情名称
     * @param duration_ms 持续时间 (0=播放完整动画)
     * @param loop 是否循环
     * @return JSON 响应字符串
     */
    std::string EmotionPlay(const std::string& emotion, uint32_t duration_ms = 0, bool loop = false);

    /**
     * @brief 获取表情列表
     * @return JSON 响应字符串
     */
    std::string EmotionList();

    /**
     * @brief 停止表情动画
     * @return JSON 响应字符串
     */
    std::string EmotionStop();

    /**
     * @brief 表情序列测试
     * @param emotions 表情名称列表
     * @param interval_ms 每个表情持续时间
     * @return JSON 响应字符串
     */
    std::string EmotionSequence(const std::vector<std::string>& emotions, uint32_t interval_ms);

    // ═══════════════════════════════════════════════════════════════
    // 综合测试接口
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 获取所有模块状态
     * @return JSON 响应字符串
     */
    std::string GetStatus();

    /**
     * @brief 执行全面自检
     * @return JSON 响应字符串
     */
    std::string SelfCheck();

    /**
     * @brief 重置所有测试模块
     * @return JSON 响应字符串
     */
    std::string Reset();

private:
    HardwareTestService();
    ~HardwareTestService();
    
    // 禁止拷贝和赋值
    HardwareTestService(const HardwareTestService&) = delete;
    HardwareTestService& operator=(const HardwareTestService&) = delete;

    // 硬件驱动实例
    std::unique_ptr<ServoDriver> servo_driver_;
    std::unique_ptr<NfcDriver> nfc_driver_;
    
    // LED 状态
    LedColor current_led_color_;
    uint8_t current_led_brightness_;
    bool led_effect_running_;

    // 表情状态
    std::string current_emotion_;
    bool emotion_running_;

    // NFC 事件回调
    std::function<void(const std::string&)> nfc_event_callback_;

    // ═══════════════════════════════════════════════════════════════
    // 内部辅助方法
    // ═══════════════════════════════════════════════════════════════

    /**
     * @brief 构建成功响应 JSON
     */
    std::string BuildSuccessResponse(const std::string& cmd, const std::string& message, cJSON* data = nullptr);

    /**
     * @brief 构建错误响应 JSON
     */
    std::string BuildErrorResponse(const std::string& cmd, int error_code, const std::string& message);

    /**
     * @brief NFC 事件处理
     */
    void OnNfcEvent(int event_type, const std::string& card_id, 
                    const std::vector<uint8_t>& card_id_bytes, uint32_t timestamp);

    /**
     * @brief 获取 LED 实例（从 Board）
     */
    void* GetLedInstance();

    /**
     * @brief 获取 Display 实例（从 Board）
     */
    void* GetDisplayInstance();
};

// ═══════════════════════════════════════════════════════════════
// 错误码定义
// ═══════════════════════════════════════════════════════════════
#define TEST_ERROR_NOT_INITIALIZED      4000
#define TEST_ERROR_NOT_SUPPORTED        4001
#define TEST_ERROR_PARAM_OUT_OF_RANGE   4002
#define TEST_ERROR_HARDWARE_FAILURE     4003
#define TEST_ERROR_NFC_NO_CARD          4004
#define TEST_ERROR_SERVO_NOT_READY      4005
#define TEST_ERROR_LED_FAILURE          4006
#define TEST_ERROR_EMOTION_NOT_FOUND    4007

#endif // _HARDWARE_TEST_SERVICE_H_

