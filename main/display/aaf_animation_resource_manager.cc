#include "aaf_animation_resource_manager.h"
#include "assets.h"
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char* TAG = "AnimResourceMgr";

// 默认的 AAF 动画文件列表（与设备状态对应）
static const char* DEFAULT_ANIMATION_NAMES[] = {
    "idle.aaf",
    "listening.aaf", 
    "speaking.aaf",
    "loading.aaf",
    "settings.aaf",
    "updating.aaf",
    "success.aaf",
    "error.aaf"
};
static const int DEFAULT_ANIMATION_COUNT = sizeof(DEFAULT_ANIMATION_NAMES) / sizeof(DEFAULT_ANIMATION_NAMES[0]);

// 默认 FPS 配置
static const int DEFAULT_FPS[] = {15, 20, 20, 15, 15, 15, 15, 15};

namespace xiaozhi {
namespace display {

AnimationResourceManager::AnimationResourceManager()
    : is_initialized_(false)
    , load_mode_(LoadMode::AssetsPartition) {
}

AnimationResourceManager::~AnimationResourceManager() {
    Deinit();
}

esp_err_t AnimationResourceManager::InitFromAssetsDefault() {
    AssetsConfig config = {
        .animation_names = DEFAULT_ANIMATION_NAMES,
        .animation_count = DEFAULT_ANIMATION_COUNT,
        .fps_array = DEFAULT_FPS
    };
    return InitFromAssets(config);
}

esp_err_t AnimationResourceManager::InitFromAssets(const AssetsConfig& config) {
    ESP_LOGI(TAG, "Initializing from Assets partition");
    
    if (is_initialized_) {
        ESP_LOGW(TAG, "Already initialized, deinitializing first");
        Deinit();
    }
    
    // 获取 Assets 实例
    auto& assets = Assets::GetInstance();
    if (!assets.partition_valid()) {
        ESP_LOGE(TAG, "Assets partition not valid");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!assets.checksum_valid()) {
        ESP_LOGW(TAG, "Assets checksum invalid, animations may not be available");
    }
    
    // 加载动画
    animations_.reserve(config.animation_count);
    int loaded_count = 0;
    
    for (int i = 0; i < config.animation_count; i++) {
        const char* name = config.animation_names[i];
        void* ptr = nullptr;
        size_t size = 0;
        
        if (assets.GetAssetData(name, ptr, size)) {
            AnimationEntry entry;
            entry.name = name;
            entry.data_address = ptr;
            entry.data_length = size;
            entry.fps = config.fps_array ? config.fps_array[i] : 15;
            
            ESP_LOGI(TAG, "Loaded AAF[%d]: '%s', size=%lu, addr=0x%08lx, fps=%d",
                     loaded_count, name, (unsigned long)size, (unsigned long)ptr, entry.fps);
            
            RegisterAnimation(entry);
            loaded_count++;
        } else {
            ESP_LOGW(TAG, "Animation not found in Assets: %s", name);
        }
    }
    
    if (loaded_count == 0) {
        ESP_LOGW(TAG, "No animations loaded from Assets partition");
        ESP_LOGW(TAG, "Make sure AAF animations are included in assets.bin");
        // 不返回错误，让系统继续运行
    }
    
    load_mode_ = LoadMode::AssetsPartition;
    is_initialized_ = true;
    
    ESP_LOGI(TAG, "Initialized %d animations from Assets partition", loaded_count);
    return ESP_OK;
}

esp_err_t AnimationResourceManager::InitFromAddresses(const AnimationAddress* addresses, int count) {
    ESP_LOGI(TAG, "Initializing from direct addresses, count=%d", count);
    
    if (is_initialized_) {
        ESP_LOGW(TAG, "Already initialized, deinitializing first");
        Deinit();
    }
    
    if (!addresses || count <= 0) {
        ESP_LOGE(TAG, "Invalid addresses or count");
        return ESP_ERR_INVALID_ARG;
    }
    
    animations_.reserve(count);
    for (int i = 0; i < count; i++) {
        if (!addresses[i].data_address) {
            ESP_LOGE(TAG, "Invalid data address at index %d", i);
            return ESP_ERR_INVALID_ARG;
        }
        
        AnimationEntry entry;
        entry.name = addresses[i].name ? addresses[i].name : ("anim_" + std::to_string(i));
        entry.data_address = addresses[i].data_address;
        entry.data_length = addresses[i].data_length;
        entry.fps = addresses[i].fps;
        
        ESP_LOGD(TAG, "Animation[%d]: %s, size=%lu, fps=%d",
                 i, entry.name.c_str(), (unsigned long)entry.data_length, entry.fps);
        
        RegisterAnimation(entry);
    }
    
    load_mode_ = LoadMode::DirectAddress;
    is_initialized_ = true;
    
    ESP_LOGI(TAG, "Successfully initialized %d animations from direct addresses", count);
    return ESP_OK;
}

esp_err_t AnimationResourceManager::InitFromFileSystem(const AnimationPath* paths, int count) {
    ESP_LOGI(TAG, "Initializing from file system, count=%d", count);
    
    if (is_initialized_) {
        ESP_LOGW(TAG, "Already initialized, deinitializing first");
        Deinit();
    }
    
    if (!paths || count <= 0) {
        ESP_LOGE(TAG, "Invalid paths or count");
        return ESP_ERR_INVALID_ARG;
    }
    
    animations_.reserve(count);
    file_data_cache_.reserve(count);
    
    for (int i = 0; i < count; i++) {
        if (!paths[i].path) {
            ESP_LOGE(TAG, "Invalid path at index %d", i);
            return ESP_ERR_INVALID_ARG;
        }
        
        // 检查文件是否存在
        struct stat st;
        if (stat(paths[i].path, &st) != 0) {
            ESP_LOGE(TAG, "File not found: %s", paths[i].path);
            return ESP_ERR_NOT_FOUND;
        }
        
        // 读取文件数据到内存
        FILE* file = fopen(paths[i].path, "rb");
        if (!file) {
            ESP_LOGE(TAG, "Failed to open file: %s", paths[i].path);
            return ESP_ERR_INVALID_STATE;
        }
        
        // 获取文件大小
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        // 读取文件内容
        std::vector<uint8_t> data(file_size);
        size_t read_size = fread(data.data(), 1, file_size, file);
        fclose(file);
        
        if (read_size != static_cast<size_t>(file_size)) {
            ESP_LOGE(TAG, "Failed to read file: %s", paths[i].path);
            return ESP_ERR_INVALID_STATE;
        }
        
        file_data_cache_.push_back(std::move(data));
        
        AnimationEntry entry;
        // 从路径中提取文件名作为动画名称
        const char* filename = strrchr(paths[i].path, '/');
        entry.name = paths[i].name ? paths[i].name : (filename ? filename + 1 : paths[i].path);
        entry.data_address = file_data_cache_.back().data();
        entry.data_length = file_data_cache_.back().size();
        entry.fps = paths[i].fps;
        
        ESP_LOGD(TAG, "Animation[%d]: %s, size=%lu, fps=%d",
                 i, entry.name.c_str(), (unsigned long)entry.data_length, entry.fps);
        
        RegisterAnimation(entry);
    }
    
    load_mode_ = LoadMode::FileSystem;
    is_initialized_ = true;
    
    ESP_LOGI(TAG, "Successfully initialized %d animations from file system", count);
    return ESP_OK;
}

const void* AnimationResourceManager::GetAnimationData(int index, size_t* out_size, int* out_fps) const {
    if (index < 0 || index >= static_cast<int>(animations_.size())) {
        ESP_LOGE(TAG, "Invalid animation index: %d (count=%d)", index, (int)animations_.size());
        return nullptr;
    }
    
    const AnimationEntry& entry = animations_[index];
    
    if (out_size) {
        *out_size = entry.data_length;
    }
    
    if (out_fps) {
        *out_fps = entry.fps;
    }
    
    return entry.data_address;
}

const void* AnimationResourceManager::GetAnimationData(const char* name, size_t* out_size, int* out_fps) const {
    int index = FindAnimationIndex(name);
    if (index < 0) {
        ESP_LOGE(TAG, "Animation not found: %s", name);
        return nullptr;
    }
    
    return GetAnimationData(index, out_size, out_fps);
}

const char* AnimationResourceManager::GetAnimationName(int index) const {
    if (index < 0 || index >= static_cast<int>(animations_.size())) {
        return nullptr;
    }
    return animations_[index].name.c_str();
}

int AnimationResourceManager::GetAnimationFps(int index) const {
    if (index < 0 || index >= static_cast<int>(animations_.size())) {
        return 15;  // 默认值
    }
    return animations_[index].fps;
}

int AnimationResourceManager::FindAnimationIndex(const char* name) const {
    if (!name) {
        return -1;
    }
    
    auto it = name_to_index_.find(name);
    if (it != name_to_index_.end()) {
        return it->second;
    }
    
    return -1;
}

void AnimationResourceManager::Deinit() {
    if (!is_initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing animation resource manager");
    
    // 清理动画列表
    ClearAnimations();
    
    // 清理文件缓存
    file_data_cache_.clear();
    
    is_initialized_ = false;
}

void AnimationResourceManager::RegisterAnimation(const AnimationEntry& entry) {
    int index = animations_.size();
    animations_.push_back(entry);
    name_to_index_[entry.name] = index;
}

void AnimationResourceManager::ClearAnimations() {
    animations_.clear();
    name_to_index_.clear();
}

} // namespace display
} // namespace xiaozhi
