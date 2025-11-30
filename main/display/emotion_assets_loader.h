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
#include <cstring>  // for strlen

namespace emotion {

// 使用宏避免与其他文件的 TAG 冲突
#define ASSETS_LOADER_TAG "EmotionAssetsLoader"

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
            ESP_LOGW(ASSETS_LOADER_TAG, "Assets partition not valid");
            return false;
        }

        void* data_ptr = nullptr;
        size_t data_size = 0;
        
        // 从 assets 获取数据
        if (!assets.GetAssetData(asset_name, data_ptr, data_size)) {
            ESP_LOGD(ASSETS_LOADER_TAG, "  ❌ Asset not found: %s", asset_name.c_str());
            return false;
        }

        ESP_LOGI(ASSETS_LOADER_TAG, "  ✅ Loaded: %s (%u bytes) -> %s", 
                 asset_name.c_str(), data_size, EmotionStateToString(emotion));

        // 缓存数据信息
        AnimationDataCache cache;
        cache.data = data_ptr;
        cache.size = data_size;
        cache.loop = loop;
        cache.asset_name = asset_name;
        
        GetAnimationCache()[emotion] = cache;

        ESP_LOGI(ASSETS_LOADER_TAG, "Registered animation: %s -> asset:%s", 
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

        // 🔧 资源名称必须与 assets 分区中的文件名完全匹配
        // assets_source/anim/ 目录下的 .json 文件会被打包到 assets 分区
        // 新动画文件: idle, listening, speaking, loading, settings, error, success, updating
        EmotionMapping mappings[] = {
            // 核心交互状态
            {EmotionState::NEUTRAL,    "idle.json",       true},   // 待机呼吸
            {EmotionState::CALM,       "idle.json",       true},   // 平静=待机
            {EmotionState::LISTENING,  "listening.json",  true},   // 倾听（声波扩散）
            {EmotionState::SPEAKING,   "speaking.json",   true},   // 说话（嘴巴张合）
            {EmotionState::THINKING,   "loading.json",    true},   // 思考=加载
            
            // 网络和系统状态 - 暂时使用 idle.json 避免切换崩溃
            {EmotionState::CONNECTING, "idle.json",       true},   // 连接中（使用 idle 避免崩溃）
            {EmotionState::BUSY,       "idle.json",       true},   // 忙碌（使用 idle 避免崩溃）
            
            // 情感状态（复用 success/error）
            {EmotionState::HAPPY,      "success.json",    true},   // 开心=成功
            {EmotionState::EXCITED,    "success.json",    true},   // 兴奋=成功
            {EmotionState::SAD,        "error.json",      true},   // 悲伤=错误表情
            {EmotionState::SLEEPY,     "idle.json",       true},   // 困倦=待机
            {EmotionState::SURPRISED,  "settings.json",   true},   // 惊讶=好奇（设置图标）
            {EmotionState::ERROR,      "error.json",      true},   // 错误（叉号+抖动）
        };

        int count = 0;
        for (const auto& m : mappings) {
            if (RegisterAnimationFromAssets(m.emotion, m.asset_name, m.loop)) {
                count++;
            }
        }

        ESP_LOGI(ASSETS_LOADER_TAG, "Registered %d animations from assets", count);
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
    // ⚠️ DEBUG: 使用简单备用动画（极简呼吸圆圈）
    // 当启用此选项时，所有动画都使用这个简单的 JSON，用于测试是否是复杂动画导致的问题
    // #define USE_SIMPLE_FALLBACK_ANIMATION
    
    // 极简 Lottie JSON：一个简单的呼吸圆圈（约 500 字节）
    // 48帧，30fps，1.6秒循环
    static constexpr const char* SIMPLE_FALLBACK_LOTTIE = R"({"v":"5.5.7","fr":30,"ip":0,"op":48,"w":160,"h":120,"assets":[],"layers":[{"ty":4,"nm":"c","sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":0,"k":[80,60,0]},"s":{"a":1,"k":[{"t":0,"s":[80,80,100],"i":{"x":[0.5],"y":[1]},"o":{"x":[0.5],"y":[0]}},{"t":24,"s":[100,100,100],"i":{"x":[0.5],"y":[1]},"o":{"x":[0.5],"y":[0]}},{"t":48,"s":[80,80,100]}]}},"shapes":[{"ty":"el","p":{"a":0,"k":[0,0]},"s":{"a":0,"k":[60,60]}},{"ty":"fl","c":{"a":0,"k":[0.3,0.6,1,1]},"o":{"a":0,"k":100}}],"ip":0,"op":48,"st":0}]})";
    
    static lottie::LottieAnimation* CreateAnimationFromAssets(lv_obj_t* parent, 
                                                               EmotionState emotion,
                                                               int32_t width, int32_t height)
    {
        void* data = nullptr;
        size_t size = 0;
        bool loop = false;

#ifdef USE_SIMPLE_FALLBACK_ANIMATION
        // 使用简单备用动画进行测试
        ESP_LOGW(ASSETS_LOADER_TAG, "⚠️ Using SIMPLE fallback animation for: %s", EmotionStateToString(emotion));
        data = const_cast<char*>(SIMPLE_FALLBACK_LOTTIE);
        size = strlen(SIMPLE_FALLBACK_LOTTIE);
        loop = true;
#else
        if (!GetAnimationData(emotion, data, size, loop)) {
            ESP_LOGE(ASSETS_LOADER_TAG, "No animation data for: %s", EmotionStateToString(emotion));
            return nullptr;
        }
#endif

        auto* anim = new lottie::LottieAnimation(parent);
        
        // 🔑 关键修复：必须先加载数据，再设置 buffer！（参考 LVGL 官方示例）
        // 官方顺序：lv_lottie_set_src_data() -> lv_lottie_set_buffer()
        if (!anim->LoadFromData(data, size)) {
            ESP_LOGE(ASSETS_LOADER_TAG, "Failed to load animation data for: %s", 
                     EmotionStateToString(emotion));
            delete anim;
            return nullptr;
        }
        
        // 加载数据后再设置 buffer
        anim->SetSize(width, height);

        ESP_LOGI(ASSETS_LOADER_TAG, "Created animation from assets: %s (%ldx%ld)", 
                 EmotionStateToString(emotion), width, height);
        
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
    ESP_LOGI(ASSETS_LOADER_TAG, "========================================");
    ESP_LOGI(ASSETS_LOADER_TAG, "🎭 Initializing Emotion System from Assets");
    ESP_LOGI(ASSETS_LOADER_TAG, "========================================");
    
    if (!display) {
        ESP_LOGW(ASSETS_LOADER_TAG, "❌ Display is null");
        return false;
    }

    // 检查 Assets 是否可用
    auto& assets = Assets::GetInstance();
    ESP_LOGI(ASSETS_LOADER_TAG, "Checking Assets partition...");
    
    if (!assets.partition_valid()) {
        ESP_LOGW(ASSETS_LOADER_TAG, "❌ Assets partition not valid, skipping emotion system");
        ESP_LOGW(ASSETS_LOADER_TAG, "   Hint: 请确保 assets 分区已正确烧录");
        return false;
    }
    ESP_LOGI(ASSETS_LOADER_TAG, "✅ Assets partition is valid");

    // 获取 LVGL 屏幕对象
    lv_obj_t* screen = lv_scr_act();
    if (!screen) {
        ESP_LOGE(ASSETS_LOADER_TAG, "❌ LVGL screen not available");
        return false;
    }
    ESP_LOGI(ASSETS_LOADER_TAG, "✅ LVGL screen available");

    // 🔧 关键修复：先注册动画，再初始化协调器
    // 1. 从 assets 分区注册动画
    ESP_LOGI(ASSETS_LOADER_TAG, "Loading Lottie animations from assets...");
    int count = EmotionAssetsLoader::RegisterStandardAnimationsFromAssets();
    ESP_LOGI(ASSETS_LOADER_TAG, "Registered %d animations from assets partition", count);

    if (count == 0) {
        ESP_LOGW(ASSETS_LOADER_TAG, "⚠️  No animations found in assets!");
        ESP_LOGW(ASSETS_LOADER_TAG, "   检查 assets_source/anim/ 目录是否包含 .json 动画文件");
        ESP_LOGW(ASSETS_LOADER_TAG, "   运行 'idf.py build flash' 重新构建并烧录");
        return false;
    }

    // 2. 初始化情感协调器（此时动画已注册，可以正常播放）
    // 注意：调用方必须已经持有 LVGL 锁！
    auto& coordinator = EmotionCoordinator::Instance();
    
    EmotionSystemConfig config;
    config.animation_base_path = "assets:";  // 特殊标记，表示从 assets 加载
    config.screen_width = width;
    config.screen_height = height;
    config.default_emotion = EmotionState::NEUTRAL;  // 使用 NEUTRAL (idle.json)
    config.auto_register_mappings = true;
    config.enable_auto_restore = true;
    config.auto_restore_delay_ms = 5000;

    if (!coordinator.Init(config, screen)) {
        ESP_LOGE(ASSETS_LOADER_TAG, "❌ Failed to initialize emotion coordinator");
        return false;
    }

    ESP_LOGI(ASSETS_LOADER_TAG, "========================================");
    ESP_LOGI(ASSETS_LOADER_TAG, "✅ Emotion System Ready!");
    ESP_LOGI(ASSETS_LOADER_TAG, "   Loaded %d animations", count);
    ESP_LOGI(ASSETS_LOADER_TAG, "   Screen: %dx%d", width, height);
    ESP_LOGI(ASSETS_LOADER_TAG, "========================================");
    
    return true;
}

/**
 * @brief 仅初始化动画管理器（简化方案，用于设备状态驱动）
 * 
 * 这是一个简化的初始化函数，只初始化 AnimationManager
 * 不涉及复杂的情感状态管理
 * 
 * @param display Display 对象
 * @param width 屏幕宽度
 * @param height 屏幕高度
 * @return true 成功
 */
inline bool InitAnimationManagerOnly(Display* display, 
                                      int width = 240,
                                      int height = 240)
{
    ESP_LOGI(ASSETS_LOADER_TAG, "========================================");
    ESP_LOGI(ASSETS_LOADER_TAG, "🎭 Initializing Animation Manager Only (Phase 1)");
    ESP_LOGI(ASSETS_LOADER_TAG, "========================================");
    
    if (!display) {
        ESP_LOGW(ASSETS_LOADER_TAG, "❌ Display is null");
        return false;
    }

    // 获取 LVGL 屏幕对象
    lv_obj_t* screen = lv_scr_act();
    if (!screen) {
        ESP_LOGE(ASSETS_LOADER_TAG, "❌ LVGL screen not available");
        return false;
    }
    ESP_LOGI(ASSETS_LOADER_TAG, "✅ LVGL screen available");

    // 只初始化 EmotionCoordinator 的 AnimationManager 部分
    auto& coordinator = EmotionCoordinator::Instance();
    
    EmotionSystemConfig config;
    config.animation_base_path = "/spiffs/anim/";  // 文件系统路径，会自动 fallback 到 Assets
    config.screen_width = width;
    config.screen_height = height;
    config.default_emotion = EmotionState::NEUTRAL;
    config.auto_register_mappings = false;  // 不自动注册情感映射
    config.enable_auto_restore = false;

    if (!coordinator.Init(config, screen)) {
        ESP_LOGE(ASSETS_LOADER_TAG, "❌ Failed to initialize animation manager");
        return false;
    }

    ESP_LOGI(ASSETS_LOADER_TAG, "========================================");
    ESP_LOGI(ASSETS_LOADER_TAG, "✅ Animation Manager Ready (Phase 1)");
    ESP_LOGI(ASSETS_LOADER_TAG, "   Screen: %dx%d", width, height);
    ESP_LOGI(ASSETS_LOADER_TAG, "   Mode: Device state driven");
    ESP_LOGI(ASSETS_LOADER_TAG, "========================================");
    
    return true;
}

} // namespace emotion

