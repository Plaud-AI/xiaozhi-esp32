/**
 * @file mmap_animations_template.h
 * @brief Animation mmap configuration template
 * 
 * 此文件是模板，实际使用时会由 esp_mmap_assets 工具自动生成
 * 生成命令：
 *   python -m esp_mmap_assets pack \
 *       --input assets/animations \
 *       --output build/assets_animations.bin \
 *       --name assets_animations \
 *       --gen_header main/assets/mmap_animations.h
 */

#pragma once

#include "esp_mmap_assets.h"

// 动画文件数量（自动生成）
#define MMAP_ANIMATIONS_FILES      18

// 校验和（自动生成）
#define MMAP_ANIMATIONS_CHECKSUM   0x00000000

// 动画索引枚举（自动生成）
enum MMAP_ANIMATION_INDEX {
    // 设备状态动画 (8个)
    ANIM_IDLE = 0,
    ANIM_LISTENING,
    ANIM_SPEAKING,
    ANIM_LOADING,
    ANIM_SETTINGS,
    ANIM_UPDATING,
    ANIM_SUCCESS,
    ANIM_ERROR,
    
    // 感情状态动画 (10个)
    ANIM_HAPPY,
    ANIM_SAD,
    ANIM_ANGRY,
    ANIM_SURPRISED,
    ANIM_CONFUSED,
    ANIM_RELAXED,
    ANIM_THINKING,
    ANIM_SLEEPY,
    ANIM_LOVING,
    ANIM_EMBARRASSED,
};

// 每个动画的 FPS 配置（自动生成）
static const int MMAP_ANIMATIONS_FPS[MMAP_ANIMATIONS_FILES] = {
    // 设备状态动画
    15,  // idle
    20,  // listening
    20,  // speaking
    15,  // loading
    15,  // settings
    15,  // updating
    24,  // success
    15,  // error
    
    // 感情状态动画
    20,  // happy
    15,  // sad
    20,  // angry
    24,  // surprised
    15,  // confused
    15,  // relaxed
    15,  // thinking
    10,  // sleepy
    20,  // loving
    15,  // embarrassed
};

// 动画文件名列表（便于调试）
static const char* MMAP_ANIMATIONS_NAMES[MMAP_ANIMATIONS_FILES] = {
    // 设备状态
    "idle.aaf",
    "listening.aaf",
    "speaking.aaf",
    "loading.aaf",
    "settings.aaf",
    "updating.aaf",
    "success.aaf",
    "error.aaf",
    
    // 感情状态
    "happy.aaf",
    "sad.aaf",
    "angry.aaf",
    "surprised.aaf",
    "confused.aaf",
    "relaxed.aaf",
    "thinking.aaf",
    "sleepy.aaf",
    "loving.aaf",
    "embarrassed.aaf",
};

