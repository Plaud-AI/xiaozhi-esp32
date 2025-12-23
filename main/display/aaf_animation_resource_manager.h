#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <esp_err.h>

namespace xiaozhi {
namespace display {

/**
 * @brief Animation Resource Manager
 * 
 * 管理动画资源的加载，支持从项目 Assets 分区获取 AAF 动画
 */
class AnimationResourceManager {
public:
    // 加载模式
    enum class LoadMode {
        AssetsPartition,  // 从 Assets 分区加载（使用项目 Assets 类）
        DirectAddress,    // 直接指定内存地址
        FileSystem        // 从文件系统加载（占用 SRAM）
    };
    
    // Assets 分区配置
    struct AssetsConfig {
        const char** animation_names;  // 动画文件名列表
        int animation_count;           // 动画数量
        const int* fps_array;          // 每个文件的 FPS 配置（可选）
    };
    
    // 直接地址配置
    struct AnimationAddress {
        const void* data_address;
        size_t data_length;
        int fps;
        const char* name;  // 可选的动画名称
    };
    
    // 文件路径配置
    struct AnimationPath {
        const char* path;
        int fps;
        const char* name;  // 可选的动画名称
    };
    
    AnimationResourceManager();
    ~AnimationResourceManager();
    
    // 禁止拷贝
    AnimationResourceManager(const AnimationResourceManager&) = delete;
    AnimationResourceManager& operator=(const AnimationResourceManager&) = delete;
    
    // 初始化（从 Assets 分区，使用项目 Assets 类）
    esp_err_t InitFromAssets(const AssetsConfig& config);
    
    // 初始化（从 Assets 分区，使用默认动画列表）
    esp_err_t InitFromAssetsDefault();
    
    // 初始化（使用直接地址）
    esp_err_t InitFromAddresses(const AnimationAddress* addresses, int count);
    
    // 初始化（从文件系统）
    esp_err_t InitFromFileSystem(const AnimationPath* paths, int count);
    
    // 获取动画数据（返回 Flash 地址，零拷贝）
    const void* GetAnimationData(int index, size_t* out_size, int* out_fps = nullptr) const;
    const void* GetAnimationData(const char* name, size_t* out_size, int* out_fps = nullptr) const;
    
    // 查询
    int GetAnimationCount() const { return animations_.size(); }
    const char* GetAnimationName(int index) const;
    int GetAnimationFps(int index) const;
    int FindAnimationIndex(const char* name) const;
    
    // 资源信息
    LoadMode GetLoadMode() const { return load_mode_; }
    bool IsInitialized() const { return is_initialized_; }
    
    // 清理
    void Deinit();
    
private:
    struct AnimationEntry {
        std::string name;
        const void* data_address;  // Flash 地址（mmap 模式）或 SRAM 地址
        size_t data_length;
        int fps;
    };
    
    bool is_initialized_;
    LoadMode load_mode_;
    
    std::vector<AnimationEntry> animations_;
    std::unordered_map<std::string, int> name_to_index_;
    
    // 用于 FileSystem 模式的数据缓存
    std::vector<std::vector<uint8_t>> file_data_cache_;
    
    // 辅助函数
    void RegisterAnimation(const AnimationEntry& entry);
    void ClearAnimations();
};

} // namespace display
} // namespace xiaozhi
