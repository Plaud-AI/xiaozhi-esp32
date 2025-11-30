#pragma once

/**
 * @file device_animation_mapper.h
 * @brief 设备状态到动画的直接映射模块（简化版）
 * 
 * 这是一个轻量级的映射模块，直接将设备状态映射到动画文件
 * 不涉及复杂的情感系统，适合简单场景使用
 * 
 * 设计理念：
 * - 设备状态 → 动画文件（一对一映射）
 * - 简单、直接、性能优先
 * - 为后续情感系统预留扩展接口
 */

#include <string>
#include <map>
#include "device_state.h"

namespace display {

/**
 * @brief 动画配置
 */
struct AnimationConfig {
    std::string file_path;      // 动画文件路径
    bool loop;                  // 是否循环播放
    
    AnimationConfig() : loop(true) {}
    AnimationConfig(const std::string& path, bool loop_mode = true)
        : file_path(path), loop(loop_mode) {}
};

/**
 * @brief 设备状态到动画的映射器
 * 
 * 核心功能：
 * - 管理设备状态到动画文件的映射关系
 * - 提供默认映射配置
 * - 支持自定义映射
 */
class DeviceAnimationMapper {
public:
    /**
     * @brief 获取单例
     */
    static DeviceAnimationMapper& GetInstance();

    /**
     * @brief 初始化
     * 
     * @param anim_base_path 动画文件基础路径（如 "/spiffs/anim/"）
     */
    void Init(const std::string& anim_base_path = "/spiffs/anim/");

    /**
     * @brief 注册设备状态到动画的映射
     * 
     * @param state 设备状态
     * @param animation_file 动画文件名（相对于 base_path）
     * @param loop 是否循环播放
     */
    void RegisterMapping(DeviceState state, const std::string& animation_file, bool loop = true);

    /**
     * @brief 注册默认映射
     * 
     * 为所有设备状态注册默认的动画文件
     * 
     * @return 成功注册的映射数量
     */
    int RegisterDefaultMappings();

    /**
     * @brief 获取设备状态对应的动画配置
     * 
     * @param state 设备状态
     * @param[out] out_config 输出动画配置
     * @return true 找到映射
     * @return false 未找到映射
     */
    bool GetAnimationConfig(DeviceState state, AnimationConfig& out_config) const;

    /**
     * @brief 获取设备状态对应的动画文件路径
     * 
     * @param state 设备状态
     * @return 动画文件完整路径（如果找到），否则返回空字符串
     */
    std::string GetAnimationPath(DeviceState state) const;

    /**
     * @brief 清除所有映射
     */
    void ClearAllMappings();

    /**
     * @brief 打印当前映射配置（调试用）
     */
    void PrintMappings() const;

private:
    DeviceAnimationMapper() = default;
    ~DeviceAnimationMapper() = default;
    DeviceAnimationMapper(const DeviceAnimationMapper&) = delete;
    DeviceAnimationMapper& operator=(const DeviceAnimationMapper&) = delete;

    bool initialized_ = false;
    std::string anim_base_path_;
    std::map<DeviceState, AnimationConfig> mappings_;
};

// ============================================================================
// 便捷函数（用于快速获取动画路径）
// ============================================================================

/**
 * @brief 获取设备状态对应的动画文件路径
 * 
 * @param state 设备状态
 * @return 动画文件路径
 */
inline std::string GetDeviceStateAnimation(DeviceState state) {
    return DeviceAnimationMapper::GetInstance().GetAnimationPath(state);
}

} // namespace display

