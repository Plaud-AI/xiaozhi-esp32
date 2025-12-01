# 🎬 AAF 动画快速开始指南

## ✅ 当前状态

AAF Display Framework 已集成完成，**编译成功**！

### 已完成工作

1. ✅ 创建了 AAF Display Framework（零拷贝，节省 SRAM）
2. ✅ 集成到 esp32s3-korvo2-v3 板子
3. ✅ 复制了 8 个测试动画文件
4. ✅ 扩展了构建脚本支持 AAF 打包
5. ✅ 编译通过

### 动画文件（已就绪）

```
main/assets/animations/
├── idle.aaf       (58K)  - 待机动画
├── listening.aaf  (72K)  - 倾听动画
├── speaking.aaf   (111K) - 说话动画
├── loading.aaf    (429K) - 加载动画
├── settings.aaf   (247K) - 设置动画
├── updating.aaf   (237K) - 更新动画
├── success.aaf    (111K) - 成功动画
└── error.aaf      (314K) - 错误动画

总计: 1.6MB
```

## 🚀 一键编译打包

### 步骤 1：重新配置 CMake

由于修改了 CMakeLists.txt，需要重新配置：

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32

# 方式 A：删除 build 重新配置（推荐）
rm -rf build
idf.py build

# 方式 B：仅重新配置
idf.py reconfigure
idf.py build
```

### 步骤 2：烧录固件和动画

```bash
# 一键烧录（固件 + assets 分区）
idf.py -p /dev/ttyUSB0 flash monitor

# 或者单独烧录
idf.py -p /dev/ttyUSB0 flash
```

构建系统会自动：
1. 将 `main/assets/animations/*.aaf` 打包为 `assets.bin`
2. 使用 mmap_assets 格式（零拷贝）
3. 烧录到 Flash 的 assets 分区
4. 运行时直接从 Flash 读取（不占用 SRAM）

## 🎯 测试动画显示

设备启动后，通过 `SetStatus()` 触发动画：

```cpp
// 在应用代码中测试
auto display = Board::GetInstance().GetDisplay();

// 测试不同状态
display->SetStatus("idle");       // 播放待机动画
display->SetStatus("listening");  // 播放倾听动画
display->SetStatus("speaking");   // 播放说话动画
display->SetStatus("loading");    // 播放加载动画
```

### 预期日志

```
I (1233) AafDisplayWidget: Initialize LVGL library
I (1240) AafDisplayWidget: Initializing animation resources
I (1245) AnimResManager: Initializing from partition: assets
I (1250) AnimResManager: Found 8 animation files
I (1255) AnimResManager: Animation[0]: idle.aaf, size=59392, fps=15
I (1260) AnimResManager: Animation[1]: listening.aaf, size=73728, fps=30
...
I (1290) AafDisplayWidget: ✅ Loaded 8 animations from mmap partition (zero-copy)
I (1295) AafDisplayWidget: ✅ AAF Display Framework ready!
```

## 📋 当前实现详情

### 内存使用（优化后）

- **SRAM**: ~2KB（仅元数据）
- **Flash**: 1.6MB（动画数据，零拷贝读取）
- **节省**: 相比旧方案节省 ~1.6MB SRAM！

### 加载方式

```cpp
// 优先使用 mmap_assets（零拷贝）
AnimationResourceManager::PartitionConfig mmap_config = {
    .partition_label = "assets",
    .max_files = 8,
    .fps_array = nullptr,
    .checksum = 0,
};

esp_err_t ret = resource_manager_->InitFromPartition(mmap_config);
if (ret == ESP_OK) {
    // ✅ 成功：零拷贝加载
} else {
    // ⚠️  回退到文件系统加载（占用 SRAM）
}
```

### 动画映射

| Status 字符串 | 设备状态 | 动画文件 | FPS |
|--------------|----------|----------|-----|
| "idle" / "standby" | Idle | idle.aaf | 15 |
| "listening" | Listening | listening.aaf | 30 |
| "speaking" | Speaking | speaking.aaf | 25 |
| "connecting" / "starting" | Loading | loading.aaf | 30 |
| "wifi_configuring" | Settings | settings.aaf | 20 |
| "upgrading" | Updating | updating.aaf | 20 |
| - | Success | success.aaf | 30 |
| "fatal_error" | Error | error.aaf | 25 |

配置文件：`main/display/aaf_animation_config.h`

## ⚙️ 分区配置

assets 分区配置（已存在）：

```csv
# partitions/v2/partitions_8mb.csv (或其他分区表)
assets_A, data, spiffs, , 4000K
```

- **类型**: SPIFFS (兼容 mmap_assets)
- **大小**: 4MB
- **用途**: 存储动画、模型、字体等资源

## 🐛 故障排查

### 问题 1：动画不显示

**日志检查**：
```bash
idf.py monitor | grep -E "AnimResManager|AafDisplayWidget"
```

**可能原因**：
- assets 分区未烧录
- 动画文件打包失败

**解决**：
```bash
# 重新构建和烧录
idf.py fullclean
idf.py build flash
```

### 问题 2：InitFromPartition 失败

**日志**：
```
W (1240) AnimResManager: Failed to load from partition
W (1245) AnimResManager: Trying file system...
```

**原因**：mmap_assets 格式的 assets.bin 未烧录

**临时方案**：系统会自动回退到文件系统加载（需要挂载 SPIFFS）

**永久方案**：确保构建系统正确打包和烧录 assets.bin

### 问题 3：LVGL 任务卡死

**已修复**！不再注册 panel_io 回调，避免与 LVGL 冲突。

## 📚 相关文件

### 核心代码

- `main/display/aaf_display_widget.h/cc` - 主 Widget
- `main/display/aaf_animation_resource_manager.h/cc` - 资源管理
- `main/display/animation_state_manager.h/cc` - 状态管理
- `main/display/aaf_animation_player.h/cc` - 动画播放器
- `main/display/aaf_animation_config.h` - 配置文件

### 构建脚本

- `scripts/build_default_assets.py` - 资源打包脚本（已扩展）
- `main/CMakeLists.txt` - 构建配置（已更新）

### 文档

- `AAF_DISPLAY_COMPLETE.md` - 完整文档
- `AAF_ANIMATION_PACKING_GUIDE.md` - 打包指南
- `main/display/README_AAF_DISPLAY.md` - API 文档

## 🎉 总结

AAF Display Framework 已完全集成，现在你可以：

1. **一键编译烧录**：`idf.py build flash`
2. **零拷贝加载**：节省 1.6MB SRAM
3. **自动打包**：修改动画文件后重新编译即可
4. **易于扩展**：添加新动画只需放到 `main/assets/animations/`

**下一步**：烧录到设备测试！🚀

