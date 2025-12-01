#include "aaf_animation_resource_manager.h"
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

static const char* TAG = "AnimResourceMgr";

namespace xiaozhi {
namespace display {

AnimationResourceManager::AnimationResourceManager()
    : is_initialized_(false)
    , load_mode_(LoadMode::MemoryMap)
    , mmap_handle_(nullptr) {
}

AnimationResourceManager::~AnimationResourceManager() {
    Deinit();
}

esp_err_t AnimationResourceManager::InitFromPartition(const PartitionConfig& config) {
    ESP_LOGI(TAG, "Initializing from partition: %s", config.partition_label);
    
    if (is_initialized_) {
        ESP_LOGW(TAG, "Already initialized, deinitializing first");
        Deinit();
    }
    
    // 配置 mmap_assets
    mmap_assets_config_t asset_config = {
        .partition_label = config.partition_label,
        .max_files = config.max_files,
        .checksum = config.checksum,
        .flags = {
            .mmap_enable = true,
            .full_check = true,
        },
    };
    
    esp_err_t ret = mmap_assets_new(&asset_config, &mmap_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create mmap assets: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 获取文件数量
    int file_num = mmap_assets_get_stored_files(mmap_handle_);
    if (file_num <= 0) {
        ESP_LOGE(TAG, "No animation files found in partition");
        mmap_assets_del(mmap_handle_);
        mmap_handle_ = nullptr;
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Found %d animation files", file_num);
    
    // 加载所有动画信息
    animations_.reserve(file_num);
    for (int i = 0; i < file_num; i++) {
        AnimationEntry entry;
        entry.name = mmap_assets_get_name(mmap_handle_, i);
        entry.data_address = mmap_assets_get_mem(mmap_handle_, i);
        entry.data_length = mmap_assets_get_size(mmap_handle_, i);
        entry.fps = config.fps_array ? config.fps_array[i] : 15;  // 默认 15 FPS
        
        ESP_LOGD(TAG, "Animation[%d]: %s, size=%zu, fps=%d, addr=%p",
                 i, entry.name.c_str(), entry.data_length, entry.fps, entry.data_address);
        
        RegisterAnimation(entry);
    }
    
    load_mode_ = LoadMode::MemoryMap;
    is_initialized_ = true;
    
    ESP_LOGI(TAG, "Successfully initialized %d animations from mmap partition", file_num);
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
        
        ESP_LOGD(TAG, "Animation[%d]: %s, size=%zu, fps=%d",
                 i, entry.name.c_str(), entry.data_length, entry.fps);
        
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
        
        ESP_LOGD(TAG, "Animation[%d]: %s, size=%zu, fps=%d",
                 i, entry.name.c_str(), entry.data_length, entry.fps);
        
        RegisterAnimation(entry);
    }
    
    load_mode_ = LoadMode::FileSystem;
    is_initialized_ = true;
    
    ESP_LOGI(TAG, "Successfully initialized %d animations from file system", count);
    return ESP_OK;
}

const void* AnimationResourceManager::GetAnimationData(int index, size_t* out_size, int* out_fps) const {
    if (index < 0 || index >= static_cast<int>(animations_.size())) {
        ESP_LOGE(TAG, "Invalid animation index: %d", index);
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
    
    // 释放 mmap 资源
    if (mmap_handle_) {
        mmap_assets_del(mmap_handle_);
        mmap_handle_ = nullptr;
    }
    
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

