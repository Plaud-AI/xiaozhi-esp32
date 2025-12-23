#pragma once

#include "lvgl.h"
#include "thorvg_capi.h"  // 🔑 ThorVG C API
#include <string>
#include <functional>
#include <vector>

namespace lottie {

/**
 * @brief Lottie 动画播放器类
 * 
 * 封装 ThorVG Lottie 动画播放功能，提供简洁的 API
 * 核心优化：
 * 1. 使用 PSRAM 存储动画 JSON 数据，避免占用 SRAM
 * 2. 使用 ThorVG 的非拷贝加载模式 (copy=false)，减少 50% 内存开销
 * 3. 使用 PSRAM 作为渲染缓冲区 (Canvas Buffer)
 */
class LottieAnimation {
public:
    /**
     * @brief 构造函数
     * 
     * @param parent LVGL 父对象，动画将作为其子对象创建
     */
    explicit LottieAnimation(lv_obj_t* parent = nullptr);
    
    /**
     * @brief 析构函数
     */
    ~LottieAnimation();

    /**
     * @brief 从文件加载 Lottie 动画
     * 
     * @param file_path 动画文件路径（如 "/spiffs/anim/idle.json"）
     * @return true 加载成功
     * @return false 加载失败
     */
    bool LoadFromFile(const char* file_path);

    /**
     * @brief 从内存数据加载 Lottie 动画
     * 
     * @param data Lottie JSON 数据指针
     * @param size 数据大小
     * @return true 加载成功
     * @return false 加载失败
     */
    bool LoadFromData(const void* data, size_t size);

    /**
     * @brief 播放动画
     * 
     * @param loop 是否循环播放，默认 true
     */
    void Play(bool loop = true);

    /**
     * @brief 暂停动画
     */
    void Pause();

    /**
     * @brief 停止动画（重置到第一帧）
     */
    void Stop();

    /**
     * @brief 跳转到指定帧
     * 
     * @param frame_num 帧号（从 0 开始）
     */
    void Seek(uint32_t frame_num);

    /**
     * @brief 设置动画播放速度
     * 
     * @param speed 播放速度倍数（1.0 = 正常速度，2.0 = 2倍速，0.5 = 0.5倍速）
     */
    void SetSpeed(float speed);

    /**
     * @brief 设置动画大小
     * 
     * @param width 宽度（像素）
     * @param height 高度（像素）
     */
    void SetSize(int32_t width, int32_t height);

    /**
     * @brief 设置动画位置
     * 
     * @param x X 坐标
     * @param y Y 坐标
     */
    void SetPosition(int32_t x, int32_t y);

    /**
     * @brief 居中显示
     */
    void Center();

    /**
     * @brief 设置完成回调
     * 
     * @param callback 动画播放完成时的回调函数
     */
    void SetCompleteCallback(std::function<void()> callback);

    /**
     * @brief 显示/隐藏动画
     * 
     * @param visible true 显示，false 隐藏
     */
    void SetVisible(bool visible);

    /**
     * @brief 获取 LVGL 对象
     * 
     * @return lv_obj_t* LVGL Canvas 对象指针
     */
    lv_obj_t* GetObject() { return canvas_obj_; }
    const lv_obj_t* GetObject() const { return canvas_obj_; }

    /**
     * @brief 获取总帧数
     * 
     * @return uint32_t 总帧数
     */
    uint32_t GetTotalFrames() const;

    /**
     * @brief 获取当前帧号
     * 
     * @return uint32_t 当前帧号
     */
    uint32_t GetCurrentFrame() const;

    /**
     * @brief 检查是否正在播放
     * 
     * @return true 正在播放
     * @return false 已暂停或停止
     */
    bool IsPlaying() const;

private:
    // 🔑 重构：使用 ThorVG C API 代替 LVGL widget
    lv_obj_t* parent_;              // 父对象（LVGL容器，用于显示）
    lv_obj_t* canvas_obj_;          // LVGL Canvas 对象（用于显示渲染结果）
    
    // ThorVG 对象
    Tvg_Canvas* tvg_canvas_;        // ThorVG 软件渲染 canvas
    Tvg_Animation* tvg_animation_;  // ThorVG 动画对象
    Tvg_Paint* tvg_picture_;        // ThorVG 图片对象（从动画获取）
    
    // 内存资源 (全部放在 PSRAM)
    uint32_t* canvas_buf_;          // 渲染画布 buffer (ARGB8888)
    char* json_data_;               // 原始 JSON 数据 buffer (用于 LoadFromData copy=false)
    size_t json_size_;              // 数据大小

    // 动画状态
    int32_t width_;                 // 动画宽度
    int32_t height_;                // 动画高度
    bool is_playing_;               // 播放状态
    bool loop_;                     // 是否循环
    float total_frames_;            // 总帧数
    float current_frame_;           // 当前帧
    float speed_;                   // 播放速度（1.0 = 正常速度）
    
    lv_timer_t* render_timer_;      // 渲染定时器（逐帧更新）
    std::function<void()> complete_callback_;  // 完成回调
    
    // 私有方法
    bool AllocateCanvas(int32_t width, int32_t height);  // 分配 canvas buffer
    void FreeCanvas();              // 释放 canvas
    void RenderFrame();             // 渲染当前帧
    static void RenderTimerCallback(lv_timer_t* timer);  // 定时器回调
};

} // namespace lottie
