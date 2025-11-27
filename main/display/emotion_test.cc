#include "emotion_animation_manager.h"
#include "animation_manager.h"
#include "esp_log.h"
#include <string>
#include <vector>

static const char* TAG = "EmotionTest";

using namespace lottie;

/**
 * @brief 情感动画测试类
 * 
 * 提供各种测试接口和示例代码
 */
class EmotionAnimationTest {
public:
    /**
     * @brief 基础初始化测试
     * 
     * 测试情感动画管理器的初始化
     */
    static bool TestInit() {
        ESP_LOGI(TAG, "=== Test: Init ===");
        
        // 初始化动画管理器（需要 LVGL 父对象）
        // AnimationManager::Instance().Init(parent_obj, 240, 240);
        
        // 初始化情感动画管理器
        if (!EmotionAnimationManager::Instance().Init()) {
            ESP_LOGE(TAG, "Failed to init EmotionAnimationManager");
            return false;
        }
        
        ESP_LOGI(TAG, "✓ Init test passed");
        return true;
    }

    /**
     * @brief 注册情感动画测试
     * 
     * 测试各种注册方式
     */
    static bool TestRegister() {
        ESP_LOGI(TAG, "=== Test: Register ===");
        
        auto& mgr = EmotionAnimationManager::Instance();
        
        // 方式 1: 单个注册
        mgr.RegisterEmotion(EmotionType::HAPPY, "/spiffs/emotions/happy.json");
        mgr.RegisterEmotion(EmotionType::SAD, "/spiffs/emotions/sad.json");
        mgr.RegisterEmotion(EmotionType::SURPRISED, "/spiffs/emotions/surprised.json");
        
        // 方式 2: 使用详细配置
        EmotionAnimConfig config;
        config.file_path = "/spiffs/emotions/angry.json";
        config.loop = false;
        config.duration_ms = 2000;
        config.speed = 1.2f;
        mgr.RegisterEmotion(EmotionType::ANGRY, config);
        
        // 方式 3: 批量注册（从目录）
        int count = mgr.RegisterEmotionsFromDirectory("/spiffs/emotions/", "{emotion}.json");
        ESP_LOGI(TAG, "Registered %d emotions from directory", count);
        
        // 验证注册
        auto registered = mgr.GetRegisteredEmotions();
        ESP_LOGI(TAG, "Total registered emotions: %zu", registered.size());
        
        ESP_LOGI(TAG, "✓ Register test passed");
        return true;
    }

    /**
     * @brief 单个情感播放测试
     * 
     * 测试显示单个情感动画
     */
    static bool TestShowSingleEmotion() {
        ESP_LOGI(TAG, "=== Test: Show Single Emotion ===");
        
        auto& mgr = EmotionAnimationManager::Instance();
        
        // 显示开心表情（播放完整动画）
        if (!mgr.ShowEmotion(EmotionType::HAPPY)) {
            ESP_LOGE(TAG, "Failed to show HAPPY emotion");
            return false;
        }
        
        ESP_LOGI(TAG, "Current emotion: %s", 
                 EmotionTypeToString(mgr.GetCurrentEmotion()));
        
        ESP_LOGI(TAG, "✓ Show single emotion test passed");
        return true;
    }

    /**
     * @brief 情感序列播放测试
     * 
     * 测试连续播放多个情感
     */
    static bool TestEmotionSequence() {
        ESP_LOGI(TAG, "=== Test: Emotion Sequence ===");
        
        auto& mgr = EmotionAnimationManager::Instance();
        
        // 创建情感序列
        std::vector<EmotionSequenceItem> sequence;
        
        // 1. 开心 - 持续 2 秒
        sequence.emplace_back(EmotionType::HAPPY, 2000, false);
        
        // 2. 惊讶 - 播放完整动画，使用过渡
        sequence.emplace_back(EmotionType::SURPRISED, 0, true);
        
        // 3. 思考 - 持续 3 秒
        sequence.emplace_back(EmotionType::THINKING, 3000, false);
        
        // 4. 兴奋 - 播放完整动画
        sequence.emplace_back(EmotionType::EXCITED, 0, false);
        
        // 播放序列（不循环）
        if (!mgr.PlayEmotionSequence(sequence, false)) {
            ESP_LOGE(TAG, "Failed to play emotion sequence");
            return false;
        }
        
        ESP_LOGI(TAG, "✓ Emotion sequence test passed");
        return true;
    }

    /**
     * @brief 循环序列播放测试
     * 
     * 测试情感序列循环播放
     */
    static bool TestLoopSequence() {
        ESP_LOGI(TAG, "=== Test: Loop Sequence ===");
        
        auto& mgr = EmotionAnimationManager::Instance();
        
        // 创建简单的情感循环
        std::vector<EmotionSequenceItem> sequence;
        sequence.emplace_back(EmotionType::HAPPY, 1500);
        sequence.emplace_back(EmotionType::CALM, 1500);
        sequence.emplace_back(EmotionType::NEUTRAL, 1500);
        
        // 循环播放
        if (!mgr.PlayEmotionSequence(sequence, true)) {
            ESP_LOGE(TAG, "Failed to play loop sequence");
            return false;
        }
        
        ESP_LOGI(TAG, "✓ Loop sequence test started (will run indefinitely)");
        return true;
    }

    /**
     * @brief 过渡动画测试
     * 
     * 测试情感切换时的过渡效果
     */
    static bool TestTransition() {
        ESP_LOGI(TAG, "=== Test: Transition ===");
        
        auto& mgr = EmotionAnimationManager::Instance();
        
        // 设置过渡动画
        mgr.SetTransitionAnimation("/spiffs/transitions/fade.json");
        
        // 创建带过渡的序列
        std::vector<EmotionSequenceItem> sequence;
        sequence.emplace_back(EmotionType::NEUTRAL, 2000, false);
        sequence.emplace_back(EmotionType::HAPPY, 2000, true);    // 使用过渡
        sequence.emplace_back(EmotionType::SAD, 2000, true);      // 使用过渡
        
        if (!mgr.PlayEmotionSequence(sequence, false)) {
            ESP_LOGE(TAG, "Failed to play transition sequence");
            return false;
        }
        
        ESP_LOGI(TAG, "✓ Transition test passed");
        return true;
    }

    /**
     * @brief 回调函数测试
     * 
     * 测试情感切换和序列完成回调
     */
    static bool TestCallbacks() {
        ESP_LOGI(TAG, "=== Test: Callbacks ===");
        
        auto& mgr = EmotionAnimationManager::Instance();
        
        // 设置情感切换回调
        mgr.SetEmotionChangeCallback([](EmotionType emotion) {
            ESP_LOGI(TAG, "🔔 Emotion changed to: %s", EmotionTypeToString(emotion));
        });
        
        // 设置序列完成回调
        mgr.SetSequenceCompleteCallback([]() {
            ESP_LOGI(TAG, "🔔 Emotion sequence completed!");
        });
        
        // 播放序列触发回调
        std::vector<EmotionSequenceItem> sequence;
        sequence.emplace_back(EmotionType::HAPPY, 1000);
        sequence.emplace_back(EmotionType::EXCITED, 1000);
        
        mgr.PlayEmotionSequence(sequence, false);
        
        ESP_LOGI(TAG, "✓ Callbacks test passed");
        return true;
    }

    /**
     * @brief 自动返回中性状态测试
     * 
     * 测试情感动画结束后自动返回中性表情
     */
    static bool TestAutoReturn() {
        ESP_LOGI(TAG, "=== Test: Auto Return Neutral ===");
        
        auto& mgr = EmotionAnimationManager::Instance();
        
        // 启用自动返回中性状态（5秒后）
        mgr.SetAutoReturnNeutral(true, 5000);
        
        // 显示一个情感
        mgr.ShowEmotion(EmotionType::HAPPY);
        
        ESP_LOGI(TAG, "Will auto return to NEUTRAL after 5 seconds");
        ESP_LOGI(TAG, "✓ Auto return test started");
        return true;
    }

    /**
     * @brief 单个情感测试接口
     * 
     * 用于快速测试某个特定情感动画
     */
    static void TestSingleEmotion(EmotionType emotion, int repeat_count = 1) {
        ESP_LOGI(TAG, "=== Testing Emotion: %s (repeat: %d) ===", 
                 EmotionTypeToString(emotion), repeat_count);
        
        auto& mgr = EmotionAnimationManager::Instance();
        mgr.TestEmotion(emotion, repeat_count);
    }

    /**
     * @brief 运行所有测试
     */
    static void RunAllTests() {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  Emotion Animation Manager Test Suite");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "");
        
        TestInit();
        TestRegister();
        TestShowSingleEmotion();
        TestEmotionSequence();
        TestCallbacks();
        TestAutoReturn();
        
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  All Tests Completed");
        ESP_LOGI(TAG, "========================================");
    }
};

// ============================================================================
// 导出的 C 接口（方便从其他代码调用）
// ============================================================================

extern "C" {

/**
 * @brief 初始化情感动画系统
 */
void emotion_anim_init() {
    EmotionAnimationTest::TestInit();
    EmotionAnimationTest::TestRegister();
}

/**
 * @brief 显示指定情感
 * 
 * @param emotion_name 情感名称（如 "happy", "sad" 等）
 */
void emotion_anim_show(const char* emotion_name) {
    if (!emotion_name) return;
    
    EmotionType emotion = StringToEmotionType(emotion_name);
    EmotionAnimationManager::Instance().ShowEmotion(emotion);
}

/**
 * @brief 测试指定情感
 * 
 * @param emotion_name 情感名称
 * @param repeat_count 重复次数
 */
void emotion_anim_test(const char* emotion_name, int repeat_count) {
    if (!emotion_name) return;
    
    EmotionType emotion = StringToEmotionType(emotion_name);
    EmotionAnimationTest::TestSingleEmotion(emotion, repeat_count);
}

/**
 * @brief 停止当前情感动画
 */
void emotion_anim_stop() {
    EmotionAnimationManager::Instance().Stop();
}

/**
 * @brief 运行所有测试
 */
void emotion_anim_run_tests() {
    EmotionAnimationTest::RunAllTests();
}

} // extern "C"

// ============================================================================
// 使用示例
// ============================================================================

namespace emotion_examples {

/**
 * @brief 示例 1: 基础使用
 */
void example_basic_usage(lv_obj_t* parent) {
    ESP_LOGI(TAG, "Example: Basic Usage");
    
    // 1. 初始化动画管理器
    AnimationManager::Instance().Init(parent, 240, 240);
    
    // 2. 初始化情感动画管理器
    EmotionAnimationManager::Instance().Init();
    
    // 3. 注册情感动画
    auto& mgr = EmotionAnimationManager::Instance();
    mgr.RegisterEmotionsFromDirectory("/spiffs/emotions/");
    
    // 4. 显示情感
    mgr.ShowEmotion(EmotionType::HAPPY);
}

/**
 * @brief 示例 2: 情感序列
 */
void example_emotion_sequence() {
    ESP_LOGI(TAG, "Example: Emotion Sequence");
    
    auto& mgr = EmotionAnimationManager::Instance();
    
    // 创建一个情感故事：开心 -> 惊讶 -> 思考 -> 平静
    std::vector<EmotionSequenceItem> story;
    story.emplace_back(EmotionType::HAPPY, 2000);
    story.emplace_back(EmotionType::SURPRISED, 1500, true);  // 带过渡
    story.emplace_back(EmotionType::THINKING, 3000, true);   // 带过渡
    story.emplace_back(EmotionType::CALM, 2000);
    
    mgr.PlayEmotionSequence(story, false);
}

/**
 * @brief 示例 3: 与 MCP 消息集成
 */
void example_mcp_integration(const char* emotion_from_ai) {
    ESP_LOGI(TAG, "Example: MCP Integration");
    
    // 从 AI 返回的情感字符串转换为情感类型
    EmotionType emotion = StringToEmotionType(emotion_from_ai);
    
    // 显示对应的情感动画
    EmotionAnimationManager::Instance().ShowEmotion(emotion);
}

/**
 * @brief 示例 4: 自定义回调
 */
void example_with_callbacks() {
    ESP_LOGI(TAG, "Example: With Callbacks");
    
    auto& mgr = EmotionAnimationManager::Instance();
    
    // 设置回调以便在情感变化时执行自定义逻辑
    mgr.SetEmotionChangeCallback([](EmotionType emotion) {
        // 例如：同步 LED 颜色
        ESP_LOGI(TAG, "Update LED for emotion: %s", EmotionTypeToString(emotion));
        
        // 例如：发送事件到其他模块
        // event_bus_post(EVENT_EMOTION_CHANGED, emotion);
    });
    
    mgr.ShowEmotion(EmotionType::HAPPY);
}

/**
 * @brief 示例 5: 动态下载情感动画
 */
void example_download_emotion() {
    ESP_LOGI(TAG, "Example: Download Emotion");
    
    // 注册一个网络动画
    EmotionAnimConfig config;
    config.file_path = "https://example.com/emotions/special.json";
    config.is_local = false;  // 标记为远程文件
    
    EmotionAnimationManager::Instance().RegisterEmotion(
        EmotionType::LOVE, config);
    
    // 播放时会自动下载（需要实现下载功能）
    // EmotionAnimationManager::Instance().ShowEmotion(EmotionType::LOVE);
}

} // namespace emotion_examples

