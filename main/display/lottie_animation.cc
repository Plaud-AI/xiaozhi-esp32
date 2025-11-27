#include "lottie_animation.h"
#include "esp_log.h"

namespace lottie {

static const char* TAG = "LottieAnimation";

LottieAnimation::LottieAnimation(lv_obj_t* parent)
    : parent_(parent ? parent : lv_scr_act())
    , lottie_obj_(nullptr)
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
    lv_lottie_set_src_file(lottie_obj_, file_path);

    ESP_LOGI(TAG, "Animation loaded successfully");
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
    lv_lottie_set_src_data(lottie_obj_, data, size);

    ESP_LOGI(TAG, "Animation loaded successfully from data");
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
        lv_obj_set_size(lottie_obj_, width, height);
    }
}

void LottieAnimation::Center()
{
    if (lottie_obj_ && parent_) {
        lv_obj_center(lottie_obj_);
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

} // namespace lottie
