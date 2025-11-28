#include "lottie_animation.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

namespace lottie {

static const char* TAG = "LottieAnimation";

LottieAnimation::LottieAnimation(lv_obj_t* parent)
    : parent_(parent ? parent : lv_scr_act())
    , lottie_obj_(nullptr)
    , buffer_(nullptr)
    , buffer_width_(0)
    , buffer_height_(0)
{
    if (!parent_) {
        ESP_LOGE(TAG, "Parent object is null and no active screen");
        return;
    }

    // 创建 Lottie 对象（LVGL 9.x API）
#if LV_USE_LOTTIE
    lottie_obj_ = lv_lottie_create(parent_);
    if (lottie_obj_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create lottie object");
        return;
    }
    
    ESP_LOGI(TAG, "Lottie animation object created successfully");
#else
    ESP_LOGE(TAG, "LV_USE_LOTTIE not enabled");
#endif
}

LottieAnimation::~LottieAnimation()
{
    FreeBuffer();
    if (lottie_obj_) {
        lv_obj_del(lottie_obj_);
        lottie_obj_ = nullptr;
    }
}

bool LottieAnimation::LoadFromFile(const char* file_path)
{
#if LV_USE_LOTTIE
    if (!lottie_obj_ || !file_path) {
        ESP_LOGE(TAG, "Invalid object or file path");
        return false;
    }

    ESP_LOGI(TAG, "Loading lottie animation from file: %s", file_path);

    // LVGL 9.x API
    // 注意：此时 buffer 应该已经通过 SetSize() 设置，所以 lv_lottie_set_src_file
    // 不会触发 invalidate 死循环
    lv_lottie_set_src_file(lottie_obj_, file_path);

    ESP_LOGI(TAG, "Animation loaded successfully from file");
    return true;
#else
    ESP_LOGE(TAG, "LV_USE_LOTTIE not enabled");
    return false;
#endif
}

bool LottieAnimation::LoadFromData(const void* data, size_t size)
{
#if LV_USE_LOTTIE
    if (!lottie_obj_ || !data || size == 0) {
        ESP_LOGE(TAG, "Invalid object or data");
        return false;
    }

    ESP_LOGI(TAG, "Loading lottie animation from data (%u bytes)", size);

    // LVGL 9.x API
    // 注意：lv_lottie_set_src_data 会立即调用 lottie_update(0) 渲染第一帧
    // 并且会更新动画的时长，动画会自动开始播放
    lv_lottie_set_src_data(lottie_obj_, data, size);

    // 🔑 关键修复：lv_lottie_set_src_data 会重置对象大小为 Lottie JSON 中的原始尺寸
    // 必须在加载数据后立即重新设置为我们的 buffer 尺寸
    if (buffer_width_ > 0 && buffer_height_ > 0) {
        lv_obj_set_size(lottie_obj_, buffer_width_, buffer_height_);
        ESP_LOGI(TAG, "✅ Re-applied object size after loading: %ldx%ld", buffer_width_, buffer_height_);
        
        lv_coord_t w = lv_obj_get_width(lottie_obj_);
        lv_coord_t h = lv_obj_get_height(lottie_obj_);
        ESP_LOGI(TAG, "✅ Final object size: %ldx%ld", w, h);
    }

    ESP_LOGI(TAG, "✅ Animation loaded successfully from data");
    return true;
#else
    ESP_LOGE(TAG, "LV_USE_LOTTIE not enabled");
    return false;
#endif
}

void LottieAnimation::Play(bool loop)
{
#if LV_USE_LOTTIE
    if (!lottie_obj_) {
        ESP_LOGE(TAG, "Lottie object is null");
        return;
    }

    // 调试：检查对象状态
    ESP_LOGI(TAG, "🎬 Play: lottie_obj=%p, parent=%p", lottie_obj_, parent_);
    ESP_LOGI(TAG, "🎬 Object visible: %d", !lv_obj_has_flag(lottie_obj_, LV_OBJ_FLAG_HIDDEN));
    ESP_LOGI(TAG, "🎬 Object size: %dx%d", 
             lv_obj_get_width(lottie_obj_), lv_obj_get_height(lottie_obj_));
    ESP_LOGI(TAG, "🎬 Object pos: (%d,%d)", 
             lv_obj_get_x(lottie_obj_), lv_obj_get_y(lottie_obj_));

    // 获取 LVGL 动画对象
    lv_anim_t* anim = lv_lottie_get_anim(lottie_obj_);
    if (anim) {
        // 设置循环
        anim->repeat_cnt = loop ? LV_ANIM_REPEAT_INFINITE : 0;
        // 开始播放
        lv_anim_start(anim);
        ESP_LOGI(TAG, "Animation started (loop=%d)", loop);
    } else {
        ESP_LOGW(TAG, "No animation object available");
    }
#else
    ESP_LOGW(TAG, "LV_USE_LOTTIE not enabled");
#endif
}

void LottieAnimation::Pause()
{
#if LV_USE_LOTTIE
    if (!lottie_obj_) return;

    lv_anim_t* anim = lv_lottie_get_anim(lottie_obj_);
    if (anim) {
        // LVGL 9.x 暂停动画需要删除动画，然后记录当前值
        // 由于 lv_anim 没有直接的暂停接口，我们可以设置速度为 0
        // 或者暂时删除动画
        ESP_LOGW(TAG, "Pause not fully implemented in LVGL 9.x");
    }
#else
    ESP_LOGW(TAG, "LV_USE_LOTTIE not enabled");
#endif
}

void LottieAnimation::Stop()
{
#if LV_USE_LOTTIE
    if (!lottie_obj_) return;

    lv_anim_t* anim = lv_lottie_get_anim(lottie_obj_);
    if (anim) {
        lv_anim_delete(lottie_obj_, nullptr);
        ESP_LOGI(TAG, "Animation stopped");
    }
#else
    ESP_LOGW(TAG, "LV_USE_LOTTIE not enabled");
#endif
}

void LottieAnimation::Seek(uint32_t frame_num)
{
#if LV_USE_LOTTIE
    if (!lottie_obj_) return;

    // LVGL 9.x 的 Lottie 不直接支持 Seek 到特定帧
    // 需要通过动画的进度百分比来设置
    ESP_LOGW(TAG, "Seek not fully supported in LVGL 9.x lottie");
#else
    ESP_LOGW(TAG, "LV_USE_LOTTIE not enabled");
#endif
}

void LottieAnimation::SetPosition(int32_t x, int32_t y)
{
    if (lottie_obj_) {
        lv_obj_set_pos(lottie_obj_, x, y);
    }
}

void LottieAnimation::SetSize(int32_t width, int32_t height)
{
    if (lottie_obj_) {
        // 🔑 关键修复：必须先设置对象大小，再分配 buffer！
        // ThorVG 需要对象大小已设置才能正确初始化内部结构
        // 参考 Demo: lv_obj_set_size() -> lv_lottie_set_buffer() -> lv_lottie_set_src_data()
        lv_obj_set_size(lottie_obj_, width, height);
        ESP_LOGI(TAG, "✅ Object size set: %ldx%ld", width, height);
        
        if (!AllocateBuffer(width, height)) {
            ESP_LOGE(TAG, "Failed to allocate buffer for %ldx%ld", width, height);
            return;
        }
        
        // 设置 buffer 到 LVGL Lottie 对象
        // 必须在 lv_lottie_set_src_data 之前调用
#if LV_USE_LOTTIE
        lv_lottie_set_buffer(lottie_obj_, width, height, buffer_);
        ESP_LOGI(TAG, "✅ Buffer set: %ldx%ld at %p", width, height, buffer_);
#endif
        
        // 确保对象可见
        lv_obj_clear_flag(lottie_obj_, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "✅ Object ready for animation");
    }
}

void LottieAnimation::Center()
{
    if (lottie_obj_ && parent_) {
        // 调试：父对象信息
        lv_coord_t pw = lv_obj_get_width(parent_);
        lv_coord_t ph = lv_obj_get_height(parent_);
        ESP_LOGI(TAG, "🎨 Parent size: %dx%d", pw, ph);
        
        // 调试：居中前的对象大小
        lv_coord_t w_before = lv_obj_get_width(lottie_obj_);
        lv_coord_t h_before = lv_obj_get_height(lottie_obj_);
        ESP_LOGI(TAG, "🎨 Before center: size(%dx%d)", w_before, h_before);
        
        lv_obj_center(lottie_obj_);
        
        // 获取对象位置和大小（用于调试）
        lv_coord_t x = lv_obj_get_x(lottie_obj_);
        lv_coord_t y = lv_obj_get_y(lottie_obj_);
        lv_coord_t w = lv_obj_get_width(lottie_obj_);
        lv_coord_t h = lv_obj_get_height(lottie_obj_);
        ESP_LOGI(TAG, "🎨 After center: pos(%d,%d) size(%dx%d)", x, y, w, h);
    }
}

void LottieAnimation::SetSpeed(float speed)
{
#if LV_USE_LOTTIE
    if (!lottie_obj_) return;

    lv_anim_t* anim = lv_lottie_get_anim(lottie_obj_);
    if (anim) {
        // LVGL 9.x 的 lv_anim_t 结构体字段已更改
        // 使用 lv_anim_set_time 来调整速度
        if (speed > 0.01f) {
            uint32_t current_time = lv_anim_get_playtime(anim);
            lv_anim_set_time(anim, (uint32_t)(current_time / speed));
        }
        ESP_LOGI(TAG, "Speed set to: %.2f", speed);
    }
#else
    ESP_LOGW(TAG, "LV_USE_LOTTIE not enabled");
#endif
}

void LottieAnimation::SetCompleteCallback(std::function<void()> callback)
{
    complete_callback_ = callback;
    
    // 如果有 lottie 对象，设置事件回调
    if (lottie_obj_) {
        lv_obj_add_event_cb(lottie_obj_, OnAnimComplete, LV_EVENT_READY, this);
    }
}

void LottieAnimation::SetVisible(bool visible)
{
    if (lottie_obj_) {
        if (visible) {
            lv_obj_clear_flag(lottie_obj_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lottie_obj_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

bool LottieAnimation::IsPlaying() const
{
#if LV_USE_LOTTIE
    if (!lottie_obj_) return false;
    
    lv_anim_t* anim = lv_lottie_get_anim(lottie_obj_);
    // 如果动画对象存在且没有被删除，则认为正在播放
    return (anim != nullptr);
#else
    return false;
#endif
}

void LottieAnimation::OnAnimComplete(lv_event_t* e)
{
    LottieAnimation* anim = static_cast<LottieAnimation*>(lv_event_get_user_data(e));
    if (anim && anim->complete_callback_) {
        anim->complete_callback_();
    }
}

uint32_t LottieAnimation::GetTotalFrames() const
{
#if LV_USE_LOTTIE
    // LVGL 9.x 没有直接获取总帧数的接口
    // 需要通过其他方式获取或者返回估算值
    return 0; // 暂时返回 0
#else
    return 0;
#endif
}

uint32_t LottieAnimation::GetCurrentFrame() const
{
#if LV_USE_LOTTIE
    // LVGL 9.x 没有直接获取当前帧的接口
    return 0; // 暂时返回 0
#else
    return 0;
#endif
}

bool LottieAnimation::AllocateBuffer(int32_t width, int32_t height)
{
    // 如果已有buffer且大小匹配，则复用
    if (buffer_ && buffer_width_ == width && buffer_height_ == height) {
        ESP_LOGI(TAG, "Reusing existing buffer (%ldx%ld)", width, height);
        return true;
    }
    
    // 释放旧buffer
    FreeBuffer();
    
    // 🔑 关键修复：使用 calloc 而不是 malloc，确保 buffer 清零
    // ThorVG 渲染需要干净的 buffer，未初始化的内存会导致崩溃
    // 分配新buffer（ARGB8888 = 4 bytes per pixel）
    size_t buffer_size = width * height * 4;
    buffer_ = heap_caps_calloc(1, buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!buffer_) {
        ESP_LOGE(TAG, "Failed to allocate buffer (%zu bytes) for %ldx%ld", 
                 buffer_size, width, height);
        return false;
    }
    
    buffer_width_ = width;
    buffer_height_ = height;
    
    ESP_LOGI(TAG, "Allocated and cleared buffer: %ldx%ld (%u bytes) at %p", 
             width, height, (unsigned int)buffer_size, buffer_);
    
    return true;
}

void LottieAnimation::FreeBuffer()
{
    if (buffer_) {
        ESP_LOGI(TAG, "Freeing buffer at %p (%ldx%ld)", 
                 buffer_, buffer_width_, buffer_height_);
        heap_caps_free(buffer_);
        buffer_ = nullptr;
        buffer_width_ = 0;
        buffer_height_ = 0;
    }
}

} // namespace lottie
