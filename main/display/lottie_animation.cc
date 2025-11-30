#include "lottie_animation.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstring>

namespace lottie {

static const char* TAG = "LottieAnimation";

LottieAnimation::LottieAnimation(lv_obj_t* parent)
    : parent_(parent ? parent : lv_scr_act())
    , canvas_obj_(nullptr)
    , tvg_canvas_(nullptr)
    , tvg_animation_(nullptr)
    , tvg_picture_(nullptr)
    , canvas_buf_(nullptr)
    , json_data_(nullptr)
    , json_size_(0)
    , width_(0)
    , height_(0)
    , is_playing_(false)
    , loop_(false)
    , total_frames_(0.0f)
    , current_frame_(0.0f)
    , speed_(1.0f)
    , render_timer_(nullptr)
{
    // 初始化 ThorVG 引擎 (线程安全单例)
    static bool tvg_initialized = false;
    if (!tvg_initialized) {
        if (tvg_engine_init(TVG_ENGINE_SW, 0) != TVG_RESULT_SUCCESS) {
            ESP_LOGE(TAG, "CRITICAL: Failed to initialize ThorVG engine");
            return;
        }
        tvg_initialized = true;
        ESP_LOGI(TAG, "ThorVG engine initialized");
    }

    // 创建 LVGL 画布对象
    canvas_obj_ = lv_canvas_create(parent_);
    if (!canvas_obj_) {
        ESP_LOGE(TAG, "Failed to create LVGL canvas object");
        return;
    }
}

LottieAnimation::~LottieAnimation()
{
    Stop(); // 停止定时器

    // 1. 销毁 ThorVG 资源
    // 注意：必须先销毁 animation，再销毁 canvas
    if (tvg_animation_) {
        tvg_animation_del(tvg_animation_);
        tvg_animation_ = nullptr;
    }
    
    if (tvg_canvas_) {
        tvg_canvas_destroy(tvg_canvas_);
        tvg_canvas_ = nullptr;
    }

    // 2. 释放 JSON 数据 buffer
    if (json_data_) {
        free(json_data_); // 对应 heap_caps_malloc/calloc
        json_data_ = nullptr;
    }

    // 3. 释放 画布 buffer
    FreeCanvas();

    // 4. 销毁 LVGL 对象
    if (canvas_obj_) {
        lv_obj_del(canvas_obj_);
        canvas_obj_ = nullptr;
    }
    
    ESP_LOGD(TAG, "Destroyed LottieAnimation instance");
}

bool LottieAnimation::LoadFromFile(const char* file_path)
{
    if (!file_path) return false;

    ESP_LOGI(TAG, "Loading Lottie file: %s", file_path);

    // 打开文件
    FILE* f = fopen(file_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return false;
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        ESP_LOGE(TAG, "File is empty: %s", file_path);
        fclose(f);
        return false;
    }

    // 释放旧数据
    if (json_data_) {
        free(json_data_);
        json_data_ = nullptr;
    }

    // 申请 PSRAM 内存 (强制)
    // +1 用于 null terminator，虽然 lottie 解析器通常看 size，但安全起见
    json_data_ = (char*)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
    if (!json_data_) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes in PSRAM for JSON", size);
        fclose(f);
        return false;
    }

    // 读取数据
    size_t read_bytes = fread(json_data_, 1, size, f);
    fclose(f);

    if (read_bytes != size) {
        ESP_LOGE(TAG, "Read error: expected %ld, got %d", size, (int)read_bytes);
        free(json_data_);
        json_data_ = nullptr;
        return false;
    }

    json_data_[size] = '\0'; // 确保字符串结束符
    json_size_ = size;

    ESP_LOGI(TAG, "File loaded to PSRAM (addr: %p, size: %ld)", json_data_, size);

    // 从内存数据加载
    return LoadFromData(json_data_, json_size_);
}

bool LottieAnimation::LoadFromData(const void* data, size_t size)
{
    if (!data || size == 0) return false;

    // 如果传入的数据不是我们自己的 buffer (json_data_)，则需要拷贝一份
    // 因为 ThorVG 使用 copy=false 模式需要数据持久化
    if (data != json_data_) {
        if (json_data_) free(json_data_);
        json_data_ = (char*)heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
        if (!json_data_) {
            ESP_LOGE(TAG, "OOM loading data");
            return false;
        }
        memcpy(json_data_, data, size);
        json_data_[size] = '\0';
        json_size_ = size;
    }

    // 创建 ThorVG 动画对象
    if (tvg_animation_) tvg_animation_del(tvg_animation_);
    tvg_animation_ = tvg_animation_new();
    
    if (!tvg_animation_) {
        ESP_LOGE(TAG, "Failed to create Tvg_Animation");
        return false;
    }

    tvg_picture_ = tvg_animation_get_picture(tvg_animation_);
    if (!tvg_picture_) return false;

    // 关键优化：copy = false (最后一个参数 0)
    // 让 ThorVG 直接解析我们保存在 PSRAM 中的 json_data_，不进行二次拷贝
    // 这是解决大文件内存崩溃的关键！
    if (tvg_picture_load_data(tvg_picture_, json_data_, size, "lottie", false) != TVG_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "ThorVG failed to parse Lottie data");
        return false;
    }

    tvg_animation_get_total_frame(tvg_animation_, &total_frames_);
    current_frame_ = 0.0f;

    ESP_LOGI(TAG, "Lottie parsed successfully. Total frames: %.1f", total_frames_);
    return true;
}

bool LottieAnimation::AllocateCanvas(int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0) return false;

    // 如果尺寸一致，复用 buffer
    if (canvas_buf_ && width_ == width && height_ == height) {
        return true;
    }

    FreeCanvas();

    // 申请 Canvas Buffer (PSRAM)
    // ARGB8888: 4 bytes per pixel
    size_t buf_size = width * height * 4;
    // 使用 heap_caps_aligned_alloc 确保缓存对齐（虽然 ThorVG 是软渲染，但 LCD DMA 可能需要）
    canvas_buf_ = (uint32_t*)heap_caps_aligned_alloc(16, buf_size, MALLOC_CAP_SPIRAM);
    
    if (!canvas_buf_) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer (%dx%d, %d bytes)", width, height, (int)buf_size);
        // 打印内存详情帮助排查
        size_t free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        ESP_LOGE(TAG, "PSRAM Free: %d, Largest Block: %d", free_size, largest_block);
        return false;
    }

    // 清零 (避免花屏)
    memset(canvas_buf_, 0, buf_size);
    
    ESP_LOGI(TAG, "Canvas buffer allocated: %d bytes", (int)buf_size);
    return true;
}

void LottieAnimation::FreeCanvas()
{
    if (canvas_buf_) {
        free(canvas_buf_); // heap_caps_aligned_alloc 也是用 free 释放
        canvas_buf_ = nullptr;
    }
}

void LottieAnimation::SetSize(int32_t width, int32_t height)
{
    if (width <= 0 || height <= 0) return;
    
    if (!AllocateCanvas(width, height)) return;

    width_ = width;
    height_ = height;

    // 重建 ThorVG Canvas
    if (tvg_canvas_) tvg_canvas_destroy(tvg_canvas_);
    tvg_canvas_ = tvg_swcanvas_create();
    
    // 绑定 Buffer
    // stride = width (pixels)
    tvg_swcanvas_set_target(tvg_canvas_, canvas_buf_, width, width, height, TVG_COLORSPACE_ARGB8888);

    // 缩放 Picture 以适应 Canvas
    if (tvg_picture_) {
        tvg_picture_set_size(tvg_picture_, width, height);
        tvg_canvas_push(tvg_canvas_, tvg_picture_);
    }

    // 更新 LVGL Canvas Buffer 指针
    lv_canvas_set_buffer(canvas_obj_, canvas_buf_, width, height, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_center(canvas_obj_);
}

void LottieAnimation::Play(bool loop)
{
    if (!tvg_animation_ || !canvas_buf_) {
        ESP_LOGW(TAG, "Cannot play: not loaded or no size set");
        return;
    }

    loop_ = loop;
    is_playing_ = true;
    current_frame_ = 0.0f;

    // 30FPS = 33ms
    uint32_t period = (uint32_t)(33.0f / speed_);
    if (period < 1) period = 1;

    if (render_timer_) {
        lv_timer_set_period(render_timer_, period);
        lv_timer_resume(render_timer_);
    } else {
        render_timer_ = lv_timer_create(RenderTimerCallback, period, this);
    }
    
    ESP_LOGI(TAG, "Animation Start: loop=%d", loop);
}

void LottieAnimation::Stop()
{
    if (render_timer_) {
        lv_timer_del(render_timer_);
        render_timer_ = nullptr;
    }
    is_playing_ = false;
}

void LottieAnimation::Pause()
{
    if (render_timer_) {
        lv_timer_pause(render_timer_);
    }
    is_playing_ = false;
}

void LottieAnimation::RenderFrame()
{
    if (!tvg_animation_ || !tvg_canvas_) return;

    // 1. 更新帧
    if (tvg_animation_set_frame(tvg_animation_, current_frame_) != TVG_RESULT_SUCCESS) return;

    // 2. 绘制到 Buffer
    // ThorVG sw canvas 会直接在 buffer 上绘制
    // 如果背景是透明的且上一帧内容残留，可能需要 clear
    // 但全屏刷新成本高，ThorVG 的 lottie 渲染通常会覆盖像素
    // 如果遇到透明背景重叠问题，可以这里 memset 0
    if (tvg_canvas_update(tvg_canvas_) == TVG_RESULT_SUCCESS) {
        tvg_canvas_draw(tvg_canvas_);
        tvg_canvas_sync(tvg_canvas_); // 等待渲染完成
    }

    // 3. 标记 LVGL 对象需要重绘 (Bitblit from buffer to screen)
    lv_obj_invalidate(canvas_obj_);
}

void LottieAnimation::RenderTimerCallback(lv_timer_t* timer)
{
    LottieAnimation* self = (LottieAnimation*)lv_timer_get_user_data(timer);
    if (!self || !self->is_playing_) return;

    self->RenderFrame();

    self->current_frame_ += 1.0f;
    if (self->current_frame_ >= self->total_frames_) {
        if (self->loop_) {
            self->current_frame_ = 0.0f;
        } else {
            self->Stop();
            if (self->complete_callback_) {
                self->complete_callback_();
            }
        }
    }
}

// Setter 实现
void LottieAnimation::SetSpeed(float speed) { 
    if(speed > 0) {
        speed_ = speed; 
        if(is_playing_ && render_timer_) {
             lv_timer_set_period(render_timer_, (uint32_t)(33.0f / speed_));
        }
    }
}
void LottieAnimation::SetPosition(int32_t x, int32_t y) { if(canvas_obj_) lv_obj_set_pos(canvas_obj_, x, y); }
void LottieAnimation::Center() { if(canvas_obj_) lv_obj_center(canvas_obj_); }
void LottieAnimation::SetVisible(bool visible) { 
    if(canvas_obj_) {
        if(visible) lv_obj_clear_flag(canvas_obj_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(canvas_obj_, LV_OBJ_FLAG_HIDDEN);
    }
}
void LottieAnimation::SetCompleteCallback(std::function<void()> callback) { complete_callback_ = callback; }
uint32_t LottieAnimation::GetTotalFrames() const { return (uint32_t)total_frames_; }
uint32_t LottieAnimation::GetCurrentFrame() const { return (uint32_t)current_frame_; }
bool LottieAnimation::IsPlaying() const { return is_playing_; }

} // namespace lottie
