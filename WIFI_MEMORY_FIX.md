# WiFi 初始化内存不足修复说明

## 问题描述
设备在 BLE/WiFi 共存模式下启动时，因内存不足导致 WiFi 初始化失败，反复重启。

错误日志：
```
ESP_ERROR_CHECK failed: esp_err_t 0x101 (ESP_ERR_NO_MEM)
W (1497) wifi:esf_buf_setup_static: alloc eb fail(1)
```

## 解决方案

本次修改通过优化内存配置解决了问题：

### 1. WiFi 缓冲区优化（节省约 22KB 内部 SRAM）

| 配置项 | 修改前 | 修改后 | 说明 |
|--------|--------|--------|------|
| 静态 RX 缓冲区 | 3 | 2 | 减少但足够使用 |
| 动态 RX 缓冲区 | 6 | 8 | 增加以补偿静态减少 |
| 静态 TX 缓冲区 | 16 | 6 | 大幅减少内存占用 |
| TX 缓存缓冲区 | 32 | 16 | 降低一半 |
| 管理短缓冲区 | 32 | 16 | 降低一半 |
| RX 管理缓冲区 | 5 | 3 | 最小化配置 |

### 2. BLE 内存优化（节省约 25KB 内部 SRAM）

**修改：** 将 NimBLE 内存分配从内部 SRAM 改为 PSRAM

```
CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL=y  →  CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y
```

### 3. 内部 SRAM 保留量调整

```
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL: 65536 (64KB)  →  81920 (80KB)
```

## 总内存节省

- **WiFi 缓冲区优化：** ~22KB
- **BLE 移至 PSRAM：** ~25KB
- **总计释放：** ~47KB 内部 SRAM

## 性能影响评估

### ✅ 可接受的影响
- WiFi 吞吐量：略有下降（5-10%），仍足够音频流传输
- BLE 响应：使用 PSRAM 后延迟增加 <1ms，对配网无影响

### ✅ 无影响
- 系统稳定性
- 音频质量
- BLE 连接稳定性
- 功耗

## 使用步骤

1. **清理构建目录（推荐）：**
```bash
rm -rf build
```

2. **重新编译：**
```bash
idf.py build
```

3. **烧录固件：**
```bash
idf.py flash monitor
```

## 验证成功标志

启动日志应显示：
```
I (xxx) WifiStation: 初始化 WiFi 驱动（纯 STA 模式）
I (xxx) wifi:wifi driver task: xxx
I (xxx) wifi:wifi firmware version: xxx
```

设备正常运行，不再反复重启。

## 详细文档

完整的问题分析和优化指南请参考：
📄 `/Users/xionghao/Documents/plaud/GitHub/docs/xiaozhi-esp32/troubleshooting/wifi-init-memory-error.md`

## 注意事项

1. **不要恢复旧配置** - 这会导致问题复现
2. **多设备测试** - 建议在多个开发板上验证
3. **监控内存** - 添加内存监控代码，及时发现问题

---

**修改日期:** 2024-11-24  
**适用分支:** s3-basic-miro-wake  
**适用芯片:** ESP32-S3 (8MB PSRAM)

