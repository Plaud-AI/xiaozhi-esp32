#pragma once

#include "lvgl.h"
#include "widgets/lottie/lv_lottie.h"
#include <string>
#include <functional>

namespace lottie {

/**
 * @brief Lottie 动画播放器类
 * 
 * 封装 ThorVG Lottie 动画播放功能，提供简洁的 API
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
     * @brief 设置播放速度
     * 
     * @param speed 速度倍率（1.0 = 正常速度，0.5 = 半速，2.0 = 2倍速）
     */
    void SetSpeed(float speed);

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
     * @return lv_obj_t* LVGL 对象指针
     */
    lv_obj_t* GetObject() { return lottie_obj_; }
    const lv_obj_t* GetObject() const { return lottie_obj_; }

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
    lv_obj_t* parent_;              // 父对象
    lv_obj_t* lottie_obj_;          // ThorVG Lottie 对象
    std::function<void()> complete_callback_;  // 完成回调
    bool is_playing_;               // 播放状态
    void* buffer_;                  // ARGB8888 buffer for rendering
    int32_t buffer_width_;          // Buffer width
    int32_t buffer_height_;         // Buffer height
    size_t buffer_size_;            // Buffer size in bytes
    lv_draw_buf_t* draw_buf_;       // LVGL draw buffer (persistent)

    static void OnAnimComplete(lv_event_t* e);
    bool AllocateBuffer(int32_t width, int32_t height);  // 分配渲染 buffer
    void FreeBuffer();              // 释放 buffer
};

} // namespace lottie

