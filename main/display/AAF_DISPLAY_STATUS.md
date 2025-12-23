# AAF Display Framework - 当前状态

**最后更新**: 2024-12-01  
**状态**: ✅ 已完成并可编译

## 概述

AAF Display Framework 已经完整实现并已加入编译系统。所有依赖已正确配置。

## 组件说明

框架包括以下组件：
- `aaf_animation_resource_manager.h/cc` - 动画资源管理器
- `animation_state_manager.h/cc` - 动画状态管理器  
- `aaf_animation_player.h/cc` - AAF 动画播放器
- `aaf_display_widget.h/cc` - 主显示控件

## 依赖配置

### ✅ 已添加的依赖

在 `main/idf_component.yml` 中已添加：

```yaml
espressif2022/image_player: "1.1.*"  # AAF Display Framework 需要
espressif/esp_mmap_assets: '>=1.2'   # 已存在
```

### ✅ 编译配置

在 `main/CMakeLists.txt` 中已添加：

```cmake
"display/aaf_animation_resource_manager.cc"
"display/animation_state_manager.cc"
"display/aaf_animation_player.cc"
"display/aaf_display_widget.cc"
```

## 如何使用

### 1. 准备动画资源

准备 `.aaf` 格式的动画文件：

- 8 个设备状态动画（idle, listening, speaking, loading, settings, updating, success, error）
- 10 个感情状态动画（根据需求定义）

每个动画文件约 200KB。

### 2. 配置动画分区

在分区表中添加或使用现有的 `assets` 分区：

```csv
# partitions/partitions_16mb.csv
assets,   data, spiffs,  ,        3M,
```

### 3. 打包动画到分区

参考 `main/assets/mmap_animations_template.h` 创建配置：

```cpp
#pragma once

// 动画文件数量
#define MMAP_ANIMATION_FILE_COUNT 8

// 动画文件校验和（由打包工具生成）
#define MMAP_ANIMATION_CHECKSUM 0x12345678

// 动画 ID 枚举
enum AnimationID {
    ANIM_IDLE = 0,
    ANIM_LISTENING,
    ANIM_SPEAKING,
    ANIM_LOADING,
    ANIM_SETTINGS,
    ANIM_UPDATING,
    ANIM_SUCCESS,
    ANIM_ERROR,
};
```

### 4. 在代码中使用

参考 `main/display/aaf_display_example.cc`：

```cpp
#include "aaf_display_widget.h"

// 初始化
auto display = std::make_unique<xiaozhi::display::AafDisplayWidget>(
    panel_io, panel, 
    SCREEN_WIDTH, SCREEN_HEIGHT
);

// 初始化动画资源（从 mmap 分区）
xiaozhi::display::AnimationResourceManager::PartitionConfig partition_config = {
    .partition_label = "assets",
    .max_files = MMAP_ANIMATION_FILE_COUNT,
    .checksum = MMAP_ANIMATION_CHECKSUM,
};
display->InitializeAnimationResources(partition_config);

// 设置设备状态
display->SetStatus("listening");  // 或 "speaking", "loading" 等

// 设置感情状态（可选，优先级更高）
display->SetEmotion("happy");  // 会打断当前的设备状态动画
```

### 5. 编译和烧录

```bash
# 更新依赖
idf.py reconfigure

# 编译
idf.py build

# 烧录（包括动画分区）
idf.py flash
```

## 框架特性

### ✅ 资源管理
- mmap_assets 零拷贝加载（最小 SRAM 使用）
- 文件系统加载支持
- 动画名称映射和查找

### ✅ 状态管理  
- 设备状态和感情状态分离
- 优先级控制（Low/Normal/High/Critical）
- 状态历史管理
- 自动状态恢复

### ✅ 动画播放
- 集成 image_player 库
- 支持循环/单次播放
- FPS 控制
- 播放完成回调

### ✅ 显示控件
- 兼容 Display 基类
- 状态栏支持
- 动画区域管理
- 超时控制

## 内存使用

### SRAM（使用 mmap_assets）
- 框架本身：~1KB
- 动画数据：0（零拷贝，直接从 Flash 读取）

### Flash（assets 分区）
- 8 个设备状态：~1.6 MB
- 10 个感情状态：~2.0 MB
- **总计约 3.6 MB**

## 性能优化

1. **零拷贝加载**：动画数据通过 mmap 直接从 Flash 访问，不占用 SRAM
2. **异步播放**：动画播放在独立任务中，不阻塞主线程
3. **智能缓存**：资源管理器自动管理动画索引
4. **硬件加速**：通过 image_player 库使用硬件加速

## 相关文档

- [框架使用指南](README_AAF_DISPLAY.md) - 详细的 API 文档
- [示例代码](aaf_display_example.cc) - 完整的使用示例
- [实现总结](AAF_DISPLAY_IMPLEMENTATION_SUMMARY.md) - 架构说明

## 故障排查

### 编译错误：找不到 anim_player.h

**解决方案**：确保 `main/idf_component.yml` 中有：
```yaml
espressif2022/image_player: "1.1.*"
```

然后运行：
```bash
idf.py reconfigure
```

### 动画不显示

**检查项**：
1. 动画分区是否正确烧录
2. 分区标签是否匹配（默认 "assets"）
3. 校验和是否正确
4. 动画文件格式是否为 .aaf

### 内存不足

**解决方案**：
- 使用 mmap_assets 加载（零拷贝）
- 减少动画文件数量或大小
- 调整分区大小

## 下一步

### 动画资源准备
1. 设计或获取 8 个设备状态动画
2. 设计或获取 10 个感情状态动画
3. 转换为 .aaf 格式

### 打包工具
需要创建一个脚本来：
1. 读取所有 .aaf 文件
2. 打包到二进制文件
3. 生成配置头文件（文件数量、校验和）
4. 烧录到 assets 分区

### 测试
1. 基本播放测试
2. 状态切换测试
3. 优先级测试
4. 长时间运行测试

## 技术支持

如有问题，请查看：
- [image_player 文档](https://github.com/espressif2022/image_player)
- [esp_mmap_assets 文档](https://components.espressif.com/components/espressif/esp_mmap_assets)
- 项目 Issue

---

**状态**: 框架已完成并可编译 ✅  
**版本**: 1.0  
**最后验证**: 2024-12-01
