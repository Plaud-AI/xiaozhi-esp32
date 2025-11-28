#include "lottie_animation.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

namespace lottie {

static const char* TAG = "LottieAnimation";

// 🔑 重构：使用 ThorVG C API 代替 LVGL Lottie widget
// 参考：/Users/xionghao/Downloads/thorvg_lottie/main/thorvg_example_main.c

LottieAnimation::LottieAnimation(lv_obj_t* parent)
    : parent_(parent ? parent : lv_scr_act())
    , canvas_obj_(nullptr)
    , tvg_canvas_(nullptr)
    , tvg_animation_(nullptr)
    , tvg_picture_(nullptr)
    , canvas_buf_(nullptr)
    , width_(0)
    , height_(0)
    , is_playing_(false)
    , loop_(false)
    , total_frames_(0.0f)
    , current_frame_(0.0f)
    , render_timer_(nullptr)
{
    if (!parent_) {
        ESP_LOGE(TAG, "Parent object is null and no active screen");
        return;
    }

    // 初始化 ThorVG 引擎（只需初始化一次）
    static bool tvg_initialized = false;
    if (!tvg_initialized) {
        if (tvg_engine_init(TVG_ENGINE_SW, 0) != TVG_RESULT_SUCCESS) {
            ESP_LOGE(TAG, "Failed to initialize ThorVG engine");
            return;
        }
        tvg_initialized = true;
        ESP_LOGI(TAG, "✅ ThorVG engine initialized");
    }

    // 创建 LVGL Canvas 对象（用于显示 ThorVG 渲染结果）
    canvas_obj_ = lv_canvas_create(parent_);
    if (!canvas_obj_) {
        ESP_LOGE(TAG, "Failed to create canvas object");
        return;
    }

    ESP_LOGI(TAG, "✅ Lottie animation object created (ThorVG C API mode)");
}

LottieAnimation::~LottieAnimation()
{
    // 停止播放
    Stop();
    
    // 删除渲染定时器
    if (render_timer_) {
        lv_timer_del(render_timer_);
        render_timer_ = nullptr;
    }
    
    // 清理 ThorVG 对象
    if (tvg_animation_) {
        tvg_animation_del(tvg_animation_);
        tvg_animation_ = nullptr;
    }
    
    if (tvg_canvas_) {
        tvg_canvas_destroy(tvg_canvas_);
        tvg_canvas_ = nullptr;
    }
    
    // 释放 canvas buffer
    FreeCanvas();
    
    // 删除 LVGL canvas 对象
    if (canvas_obj_) {
        lv_obj_del(canvas_obj_);
        canvas_obj_ = nullptr;
    }
}

bool LottieAnimation::LoadFromFile(const char* file_path)
{
    ESP_LOGW(TAG, "LoadFromFile not implemented in ThorVG C API mode");
    return false;
}

bool LottieAnimation::LoadFromData(const void* data, size_t size)
{
    if (!canvas_obj_ || !data || size == 0) {
        ESP_LOGE(TAG, "Invalid object or data");
        return false;
    }

    ESP_LOGI(TAG, "Loading lottie animation from data (%u bytes)", size);

    // 创建 ThorVG 动画对象
    if (tvg_animation_) {
        tvg_animation_del(tvg_animation_);
    }
    
    tvg_animation_ = tvg_animation_new();
    if (!tvg_animation_) {
        ESP_LOGE(TAG, "Failed to create ThorVG animation");
        return false;
    }
    
    // 获取 picture 对象
    tvg_picture_ = tvg_animation_get_picture(tvg_animation_);
    if (!tvg_picture_) {
        ESP_LOGE(TAG, "Failed to get picture from animation");
        return false;
    }
    
    // 加载 Lottie 数据（JSON）
    if (tvg_picture_load_data(tvg_picture_, data, size, "lottie", 1) != TVG_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Failed to load lottie data");
        return false;
    }
    
    // 获取总帧数
    tvg_animation_get_total_frame(tvg_animation_, &total_frames_);
    current_frame_ = 0.0f;
    
    ESP_LOGI(TAG, "✅ Animation loaded: %.0f frames", total_frames_);
    return true;
}

void LottieAnimation::Play(bool loop)
{
    if (!tvg_animation_ || !canvas_obj_) {
        ESP_LOGE(TAG, "Animation or canvas not ready");
        return;
    }

    loop_ = loop;
    is_playing_ = true;
    current_frame_ = 0.0f;
    
    // 启动渲染定时器（30 FPS = 33ms per frame）
    if (!render_timer_) {
        render_timer_ = lv_timer_create(RenderTimerCallback, 33, this);
    } else {
        lv_timer_resume(render_timer_);
    }
    
    ESP_LOGI(TAG, "🎬 Animation started (loop=%d, frames=%.0f)", loop, total_frames_);
}

void LottieAnimation::Pause()
{
    if (render_timer_) {
        lv_timer_pause(render_timer_);
    }
    is_playing_ = false;
    ESP_LOGI(TAG, "Animation paused");
}

void LottieAnimation::Stop()
{
    if (render_timer_) {
        lv_timer_pause(render_timer_);
    }
    is_playing_ = false;
    current_frame_ = 0.0f;
    ESP_LOGI(TAG, "Animation stopped");
}

void LottieAnimation::Seek(uint32_t frame_num)
{
    if (!tvg_animation_) return;
    
    current_frame_ = (float)frame_num;
    if (current_frame_ >= total_frames_) {
        current_frame_ = total_frames_ - 1.0f;
    }
    
    // 立即渲染这一帧
    RenderFrame();
}

void LottieAnimation::SetPosition(int32_t x, int32_t y)
{
    if (canvas_obj_) {
        lv_obj_set_pos(canvas_obj_, x, y);
    }
}

void LottieAnimation::SetSize(int32_t width, int32_t height)
{
    if (!canvas_obj_ || !tvg_animation_) return;
    
    // 分配 canvas buffer
    if (!AllocateCanvas(width, height)) {
        ESP_LOGE(TAG, "Failed to allocate canvas for %ldx%ld", width, height);
        return;
    }
    
    width_ = width;
    height_ = height;
    
    // 创建 ThorVG canvas
    if (tvg_canvas_) {
        tvg_canvas_destroy(tvg_canvas_);
    }
    
    tvg_canvas_ = tvg_swcanvas_create();
    if (!tvg_canvas_) {
        ESP_LOGE(TAG, "Failed to create ThorVG canvas");
        return;
    }
    
    // 设置 canvas 渲染目标（参考官方 demo line 119）
    tvg_swcanvas_set_target(tvg_canvas_, canvas_buf_, width, width, height, TVG_COLORSPACE_ARGB8888);
    
    // 设置 picture 大小
    if (tvg_picture_) {
        tvg_picture_set_size(tvg_picture_, width, height);
    }
    
    // 将 picture 推送到 canvas
    if (tvg_picture_) {
        tvg_canvas_push(tvg_canvas_, tvg_picture_);
    }
    
    // 设置 LVGL canvas 的 buffer
    lv_canvas_set_buffer(canvas_obj_, canvas_buf_, width, height, LV_COLOR_FORMAT_ARGB8888);
    
    // 居中对象
    lv_obj_center(canvas_obj_);
    
    ESP_LOGI(TAG, "✅ Canvas configured: %ldx%ld", width, height);
}

void LottieAnimation::Center()
{
    if (canvas_obj_) {
        lv_obj_center(canvas_obj_);
    }
}

void LottieAnimation::SetVisible(bool visible)
{
    if (canvas_obj_) {
        if (visible) {
            lv_obj_clear_flag(canvas_obj_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(canvas_obj_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void LottieAnimation::SetOnCompleteCallback(std::function<void()> callback)
{
    complete_callback_ = callback;
}

uint32_t LottieAnimation::GetTotalFrames() const
{
    return (uint32_t)total_frames_;
}

uint32_t LottieAnimation::GetCurrentFrame() const
{
    return (uint32_t)current_frame_;
}

bool LottieAnimation::IsPlaying() const
{
    return is_playing_;
}

// ============================================================================
// 私有方法
// ============================================================================

bool LottieAnimation::AllocateCanvas(int32_t width, int32_t height)
{
    // 如果已有 buffer 且大小匹配，则复用
    if (canvas_buf_ && width_ == width && height_ == height) {
        ESP_LOGI(TAG, "Reusing existing canvas buffer (%ldx%ld)", width, height);
        return true;
    }
    
    // 释放旧 buffer
    FreeCanvas();
    
    // 分配 ARGB8888 buffer（参考官方 demo line 264）
    canvas_buf_ = (uint32_t*)heap_caps_calloc(width * height, sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    
    if (!canvas_buf_) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer for %ldx%ld", width, height);
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Allocated canvas buffer: %ldx%ld (%u bytes) at %p", 
             width, height, (unsigned int)(width * height * 4), canvas_buf_);
    
    return true;
}

void LottieAnimation::FreeCanvas()
{
    if (canvas_buf_) {
        heap_caps_free(canvas_buf_);
        canvas_buf_ = nullptr;
        ESP_LOGI(TAG, "Canvas buffer freed");
    }
}

void LottieAnimation::RenderFrame()
{
    if (!tvg_canvas_ || !tvg_animation_ || !canvas_buf_) {
        return;
    }
    
    // 设置当前帧（参考官方 demo line 150）
    tvg_animation_set_frame(tvg_animation_, current_frame_);
    
    // 更新 canvas
    tvg_canvas_update(tvg_canvas_);
    
    // 渲染到 buffer
    tvg_canvas_draw(tvg_canvas_);
    
    // 等待渲染完成
    tvg_canvas_sync(tvg_canvas_);
    
    // 通知 LVGL 更新 canvas 显示
    if (canvas_obj_) {
        lv_obj_invalidate(canvas_obj_);
    }
}

void LottieAnimation::RenderTimerCallback(lv_timer_t* timer)
{
    auto* self = static_cast<LottieAnimation*>(lv_timer_get_user_data(timer));
    if (!self || !self->is_playing_) {
        return;
    }
    
    // 渲染当前帧
    self->RenderFrame();
    
    // 前进到下一帧
    self->current_frame_ += 1.0f;
    
    // 检查是否播放完成
    if (self->current_frame_ >= self->total_frames_) {
        if (self->loop_) {
            // 循环播放：回到第一帧
            self->current_frame_ = 0.0f;
        } else {
            // 停止播放
            self->Stop();
            
            // 调用完成回调
            if (self->complete_callback_) {
                self->complete_callback_();
            }
        }
    }
}

} // namespace lottie
