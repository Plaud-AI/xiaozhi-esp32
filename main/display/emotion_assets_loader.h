#pragma once

/**
 * @file emotion_assets_loader.h
 * @brief 从 Assets 分区加载 Lottie 动画
 * 
 * 使用项目原有的 Assets 类和 memory-mapped 方式加载动画
 * 不需要文件系统，直接从内存加载
 */

#include "assets.h"
#include "lottie_animation.h"
#include "emotion_coordinator.h"
#include "display.h"
#include "esp_log.h"
#include <string>
#include <map>

namespace emotion {

static const char* TAG = "EmotionAssetsLoader";

/**
 * @brief 从 Assets 分区加载动画的助手类
 */
class EmotionAssetsLoader {
public:
    /**
     * @brief 从 assets 分区注册动画
     * 
     * @param emotion 情感类型
     * @param asset_name Assets 中的资源名称（如 "happy"）
     * @param loop 是否循环
     * @return true 成功
     */
    static bool RegisterAnimationFromAssets(EmotionState emotion, 
                                           const std::string& asset_name,
                                           bool loop = false)
    {
        auto& assets = Assets::GetInstance();
        
        // 检查 assets 分区是否有效
        if (!assets.partition_valid()) {
            ESP_LOGW(TAG, "Assets partition not valid");
            return false;
        }

        void* data_ptr = nullptr;
        size_t data_size = 0;
        
        // 从 assets 获取数据
        if (!assets.GetAssetData(asset_name, data_ptr, data_size)) {
            ESP_LOGW(TAG, "Asset not found: %s", asset_name.c_str());
            return false;
        }

        ESP_LOGI(TAG, "Loaded asset: %s (%u bytes)", asset_name.c_str(), data_size);

        // 缓存数据信息
        AnimationDataCache cache;
        cache.data = data_ptr;
        cache.size = data_size;
        cache.loop = loop;
        cache.asset_name = asset_name;
        
        GetAnimationCache()[emotion] = cache;

        ESP_LOGI(TAG, "Registered animation: %s -> asset:%s", 
                 EmotionStateToString(emotion), asset_name.c_str());
        
        return true;
    }

    /**
     * @brief 批量注册标准情感动画
     * 
     * 从 assets 分区注册所有标准情感动画
     * 假设 assets 中的命名为：happy, sad 等
     * 
     * @return 成功注册的数量
     */
    static int RegisterStandardAnimationsFromAssets()
    {
        struct EmotionMapping {
            EmotionState emotion;
            const char* asset_name;
            bool loop;
        };

        EmotionMapping mappings[] = {
            {EmotionState::HAPPY,      "happy",      true},   // 循环播放，持续展示
            {EmotionState::SAD,        "sad",        true},   // 循环播放，持续展示
            {EmotionState::EXCITED,    "excited",    true},   // 循环播放，持续展示
            {EmotionState::CALM,       "calm",       true},   // 循环播放，持续展示
            {EmotionState::SLEEPY,     "sleepy",     true},   // 循环播放，持续展示
            {EmotionState::SURPRISED,  "surprised",  true},   // 循环播放，持续展示
            {EmotionState::LISTENING,  "listening",  true},   // 循环播放
            {EmotionState::THINKING,   "thinking",   true},   // 循环播放
            {EmotionState::SPEAKING,   "speaking",   true},   // 循环播放
        };

        int count = 0;
        for (const auto& m : mappings) {
            if (RegisterAnimationFromAssets(m.emotion, m.asset_name, m.loop)) {
                count++;
            }
        }

        ESP_LOGI(TAG, "Registered %d animations from assets", count);
        return count;
    }

    /**
     * @brief 从缓存获取动画数据
     * 
     * @param emotion 情感类型
     * @param[out] out_data 输出数据指针
     * @param[out] out_size 输出数据大小
     * @param[out] out_loop 是否循环
     * @return true 找到数据
     */
    static bool GetAnimationData(EmotionState emotion, 
                                void*& out_data, 
                                size_t& out_size,
                                bool& out_loop)
    {
        auto& cache = GetAnimationCache();
        auto it = cache.find(emotion);
        
        if (it == cache.end()) {
            return false;
        }

        out_data = it->second.data;
        out_size = it->second.size;
        out_loop = it->second.loop;
        
        return true;
    }

    /**
     * @brief 创建并加载 Lottie 动画（从 assets 数据）
     * 
     * @param parent LVGL 父对象
     * @param emotion 情感类型
     * @return LottieAnimation* 动画对象（失败返回 nullptr）
     */
    static lottie::LottieAnimation* CreateAnimationFromAssets(lv_obj_t* parent, 
                                                               EmotionState emotion)
    {
        void* data = nullptr;
        size_t size = 0;
        bool loop = false;

        if (!GetAnimationData(emotion, data, size, loop)) {
            ESP_LOGE(TAG, "No animation data for: %s", EmotionStateToString(emotion));
            return nullptr;
        }

        auto* anim = new lottie::LottieAnimation(parent);
        
        // 使用内存数据加载（关键！）
        if (!anim->LoadFromData(data, size)) {
            ESP_LOGE(TAG, "Failed to load animation data for: %s", 
                     EmotionStateToString(emotion));
            delete anim;
            return nullptr;
        }

        ESP_LOGI(TAG, "Created animation from assets: %s", EmotionStateToString(emotion));
        
        return anim;
    }

private:
    struct AnimationDataCache {
        void* data;
        size_t size;
        bool loop;
        std::string asset_name;
    };

    static std::map<EmotionState, AnimationDataCache>& GetAnimationCache() {
        static std::map<EmotionState, AnimationDataCache> cache;
        return cache;
    }
};

/**
 * @brief 初始化情感系统（使用 Assets 分区）
 * 
 * 这是沿用项目原有 Assets 方案的初始化函数
 * 
 * @param display Display 对象
 * @param width 屏幕宽度
 * @param height 屏幕高度
 * @return true 成功
 */
inline bool InitEmotionSystemFromAssets(Display* display, 
                                        int width = 240,
                                        int height = 240)
{
    if (!display) {
        ESP_LOGW(TAG, "Display is null");
        return false;
    }

    ESP_LOGI(TAG, "Initializing emotion system from Assets partition");

    // 检查 Assets 是否可用
    auto& assets = Assets::GetInstance();
    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition not valid, skipping emotion system");
        return false;
    }

    // 获取 LVGL 屏幕对象
    lv_obj_t* screen = lv_scr_act();
    if (!screen) {
        ESP_LOGE(TAG, "LVGL screen not available");
        return false;
    }

    // 🔧 关键修复：先注册动画，再初始化协调器
    // 1. 从 assets 分区注册动画
    int count = EmotionAssetsLoader::RegisterStandardAnimationsFromAssets();
    ESP_LOGI(TAG, "Registered %d animations from assets partition", count);

    if (count == 0) {
        ESP_LOGW(TAG, "No animations found in assets, emotion system may not work");
        return false;
    }

    // 2. 初始化情感协调器（此时动画已注册，可以正常播放）
    auto& coordinator = EmotionCoordinator::Instance();
    
    EmotionSystemConfig config;
    config.animation_base_path = "assets:";  // 特殊标记，表示从 assets 加载
    config.screen_width = width;
    config.screen_height = height;
    config.default_emotion = EmotionState::CALM;
    config.auto_register_mappings = true;
    config.enable_auto_restore = true;
    config.auto_restore_delay_ms = 5000;

    if (!coordinator.Init(config, screen)) {
        ESP_LOGE(TAG, "Failed to initialize emotion coordinator");
        return false;
    }

    ESP_LOGI(TAG, "Emotion system initialized from Assets successfully!");
    
    return true;
}

} // namespace emotion

