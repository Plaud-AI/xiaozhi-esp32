#pragma once

/**
 * @file mount_assets_spiffs.h
 * @brief 挂载 assets 分区为 SPIFFS 文件系统
 * 
 * 将 assets 分区（原本用于 memory-mapped assets）挂载为 SPIFFS，
 * 用于存储 Lottie 动画等文件
 */

#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_partition.h>
#include <stdio.h>

namespace assets_mount {

static const char* TAG = "AssetsSPIFFS";

/**
 * @brief 挂载 assets 分区为 SPIFFS
 * 
 * @return true 挂载成功
 * @return false 挂载失败
 */
inline bool MountAssetsAsSPIFFS() {
    ESP_LOGI(TAG, "Mounting assets partition as SPIFFS...");

    // 检查 assets 分区是否存在
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 
        "assets"
    );

    if (partition == nullptr) {
        ESP_LOGE(TAG, "Assets partition not found!");
        return false;
    }

    ESP_LOGI(TAG, "Found assets partition: size=%lu KB, address=0x%lx", 
             partition->size / 1024, partition->address);

    // 配置 SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/assets",              // 挂载点
        .partition_label = "assets",         // 分区标签
        .max_files = 10,                     // 最大同时打开文件数
        .format_if_mount_failed = false      // 不自动格式化（避免破坏数据）
    };

    // 挂载 SPIFFS
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return false;
    }

    // 获取分区信息
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("assets", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        // 继续执行，因为挂载可能成功了
    } else {
        ESP_LOGI(TAG, "SPIFFS: Total=%lu KB, Used=%lu KB, Free=%lu KB",
                 total / 1024, used / 1024, (total - used) / 1024);
    }

    // 测试访问
    FILE* f = fopen("/assets/anim/happy.json", "r");
    if (f) {
        ESP_LOGI(TAG, "✅ Test file access successful: /assets/anim/happy.json");
        fclose(f);
    } else {
        ESP_LOGW(TAG, "⚠️  Cannot access test file (may need to flash SPIFFS first)");
    }

    ESP_LOGI(TAG, "Assets SPIFFS mounted at: /assets");
    return true;
}

/**
 * @brief 卸载 assets SPIFFS
 */
inline void UnmountAssetsAsSPIFFS() {
    esp_vfs_spiffs_unregister("assets");
    ESP_LOGI(TAG, "Assets SPIFFS unmounted");
}

/**
 * @brief 格式化 assets 分区为 SPIFFS（危险操作！会清空所有数据）
 * 
 * 仅在首次使用或需要重建文件系统时调用
 * 
 * @return true 格式化成功
 */
inline bool FormatAssetsAsSPIFFS() {
    ESP_LOGW(TAG, "⚠️  Formatting assets partition as SPIFFS...");
    
    // 先卸载
    esp_vfs_spiffs_unregister("assets");
    
    // 格式化
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, 
        ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 
        "assets"
    );
    
    if (!partition) {
        ESP_LOGE(TAG, "Partition not found");
        return false;
    }
    
    esp_err_t ret = esp_spiffs_format("assets");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to format: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Format successful");
    
    // 重新挂载
    return MountAssetsAsSPIFFS();
}

} // namespace assets_mount


