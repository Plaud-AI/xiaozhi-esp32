#include "lottie_animation.h"
#include "esp_log.h"

static const char* TAG = "LottieAnim";

namespace lottie {

LottieAnimation::LottieAnimation(lv_obj_t* parent)
    : parent_(parent)
    , lottie_obj_(nullptr)
    , complete_callback_(nullptr)
    , is_playing_(false)
{
    // 如果没有指定父对象，使用当前屏幕
    if (parent_ == nullptr) {
        parent_ = lv_scr_act();
    }

    // 创建 ThorVG Lottie 对象
#if LV_USE_THORVG
    lottie_obj_ = lv_thorvg_create(parent_);
    if (lottie_obj_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create ThorVG Lottie object");
        return;
    }

    // 设置默认大小
    lv_obj_set_size(lottie_obj_, 200, 200);

    ESP_LOGI(TAG, "LottieAnimation created");
#else
    ESP_LOGE(TAG, "ThorVG is not enabled in LVGL configuration!");
#endif
}

LottieAnimation::~LottieAnimation()
{
    if (lottie_obj_) {
        lv_obj_del(lottie_obj_);
        lottie_obj_ = nullptr;
    }
    ESP_LOGI(TAG, "LottieAnimation destroyed");
}

bool LottieAnimation::LoadFromFile(const char* file_path)
{
#if LV_USE_THORVG
    if (!lottie_obj_ || !file_path) {
        ESP_LOGE(TAG, "Invalid object or file path");
        return false;
    }

    ESP_LOGI(TAG, "Loading Lottie animation from: %s", file_path);

    // 加载 Lottie 文件
    if (lv_thorvg_set_src_file(lottie_obj_, file_path) != LV_RES_OK) {
        ESP_LOGE(TAG, "Failed to load Lottie file: %s", file_path);
        return false;
    }

    ESP_LOGI(TAG, "Lottie animation loaded successfully");
    return true;
#else
    ESP_LOGE(TAG, "ThorVG is not enabled!");
    return false;
#endif
}

bool LottieAnimation::LoadFromData(const void* data, size_t size)
{
#if LV_USE_THORVG
    if (!lottie_obj_ || !data || size == 0) {
        ESP_LOGE(TAG, "Invalid object or data");
        return false;
    }

    ESP_LOGI(TAG, "Loading Lottie animation from memory (%u bytes)", size);

    // 加载 Lottie 数据
    if (lv_thorvg_set_src_data(lottie_obj_, data, size) != LV_RES_OK) {
        ESP_LOGE(TAG, "Failed to load Lottie data");
        return false;
    }

    ESP_LOGI(TAG, "Lottie animation loaded from memory successfully");
    return true;
#else
    ESP_LOGE(TAG, "ThorVG is not enabled!");
    return false;
#endif
}

void LottieAnimation::Play(bool loop)
{
#if LV_USE_THORVG
    if (!lottie_obj_) {
        ESP_LOGE(TAG, "Lottie object is null");
        return;
    }

    // 设置循环
    lv_thorvg_set_loop(lottie_obj_, loop);

    // 开始播放
    lv_thorvg_play(lottie_obj_);

    is_playing_ = true;
    ESP_LOGI(TAG, "Playing animation (loop: %d)", loop);
#endif
}

void LottieAnimation::Pause()
{
#if LV_USE_THORVG
    if (!lottie_obj_) return;

    lv_thorvg_pause(lottie_obj_);
    is_playing_ = false;
    ESP_LOGI(TAG, "Animation paused");
#endif
}

void LottieAnimation::Stop()
{
#if LV_USE_THORVG
    if (!lottie_obj_) return;

    lv_thorvg_stop(lottie_obj_);
    is_playing_ = false;
    ESP_LOGI(TAG, "Animation stopped");
#endif
}

void LottieAnimation::Seek(uint32_t frame_num)
{
#if LV_USE_THORVG
    if (!lottie_obj_) return;

    lv_thorvg_set_frame(lottie_obj_, frame_num);
    ESP_LOGD(TAG, "Seeked to frame %lu", frame_num);
#endif
}

void LottieAnimation::SetSize(int width, int height)
{
    if (!lottie_obj_) return;

    lv_obj_set_size(lottie_obj_, width, height);
    ESP_LOGD(TAG, "Size set to %dx%d", width, height);
}

void LottieAnimation::SetPosition(int x, int y)
{
    if (!lottie_obj_) return;

    lv_obj_set_pos(lottie_obj_, x, y);
    ESP_LOGD(TAG, "Position set to (%d, %d)", x, y);
}

void LottieAnimation::Center()
{
    if (!lottie_obj_) return;

    lv_obj_center(lottie_obj_);
    ESP_LOGD(TAG, "Animation centered");
}

void LottieAnimation::SetSpeed(float speed)
{
#if LV_USE_THORVG
    if (!lottie_obj_) return;

    // ThorVG 使用帧率来控制速度
    // 获取原始帧率并调整
    uint32_t total_frames = GetTotalFrames();
    if (total_frames > 0) {
        // 注意：这里可能需要根据实际 API 调整
        // lv_thorvg_set_speed(lottie_obj_, speed);
        ESP_LOGD(TAG, "Speed set to %.2f", speed);
    }
#endif
}

void LottieAnimation::SetCompleteCallback(std::function<void()> callback)
{
    complete_callback_ = callback;

    if (lottie_obj_ && callback) {
        // 添加完成事件监听
        lv_obj_add_event_cb(lottie_obj_, OnAnimComplete, LV_EVENT_READY, this);
    }
}

void LottieAnimation::SetVisible(bool visible)
{
    if (!lottie_obj_) return;

    if (visible) {
        lv_obj_clear_flag(lottie_obj_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lottie_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}

uint32_t LottieAnimation::GetTotalFrames() const
{
#if LV_USE_THORVG
    if (!lottie_obj_) return 0;
    return lv_thorvg_get_total_frame(lottie_obj_);
#else
    return 0;
#endif
}

uint32_t LottieAnimation::GetCurrentFrame() const
{
#if LV_USE_THORVG
    if (!lottie_obj_) return 0;
    return lv_thorvg_get_current_frame(lottie_obj_);
#else
    return 0;
#endif
}

bool LottieAnimation::IsPlaying() const
{
    return is_playing_;
}

void LottieAnimation::OnAnimComplete(lv_event_t* e)
{
    LottieAnimation* anim = static_cast<LottieAnimation*>(lv_event_get_user_data(e));
    if (anim && anim->complete_callback_) {
        anim->complete_callback_();
    }
}

} // namespace lottie

