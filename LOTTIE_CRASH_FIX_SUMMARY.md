# Lottie 动画崩溃修复总结

## 修复日期
2025-11-29

## 问题描述
设备在唤醒后对话、播放 Lottie 表情动画时异常崩溃重启。崩溃类型：`LoadProhibited`，地址：`0x00000016`。

## 根本原因
1. **在 LVGL 任务中使用 vTaskDelay**（最严重）- 阻塞显示系统导致内存损坏
2. 定时器只暂停不删除 - timer 回调访问已释放的内存
3. ThorVG API 缺少错误检查
4. Canvas buffer 分配缺少参数验证

## 修复文件

### 1. main/display/emotion_coordinator.cc
- ❌ 移除：在 LVGL 任务中的 `vTaskDelay` 调用
- ✅ 改进：依赖 `Stop()` 的同步 timer 删除

### 2. main/display/lottie_animation.cc
- ✅ `Stop()`: 删除 timer 而不是暂停
- ✅ 析构函数：严格的清理顺序
- ✅ `RenderFrame()`: 全面的状态和返回值检查
- ✅ `AllocateCanvas()`: 参数验证和溢出保护
- ✅ `SetSize()`: ThorVG API 返回值检查

### 3. main/audio/audio_service.cc
- ✅ `AudioInputTask()`: event_group_ 有效性检查
- ✅ 构造函数：记录 event_group_ 地址
- ✅ 析构函数：安全的清理顺序

## 关键修复点

### 修复前
```cpp
// ❌ 在 LVGL 任务回调中
current_asset_anim_->Stop();
vTaskDelay(pdMS_TO_TICKS(10));  // 阻塞 LVGL 任务！
delete current_asset_anim_;
```

### 修复后
```cpp
// ✅ LVGL timer 删除是同步的
current_asset_anim_->Stop();  // 立即删除 timer
delete current_asset_anim_;   // 安全删除
```

## 测试建议
1. 快速切换动画测试（100次循环）
2. 长时间运行测试（24小时）
3. 并发场景：唤醒 + TTS + 动画切换

## 详细文档
- `/Users/xionghao/Documents/GitHub/docs/xiaozhi-esp32/troubleshooting/lottie-animation-crash-fix-v2.md`
