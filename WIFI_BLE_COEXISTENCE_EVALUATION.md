# WiFi/BLE 并行运行可行性评估

**日期**: 2025-12-01  
**目标**: 实现 WiFi 和 BLE 同时运行，通过 BLE 连接设备并配置  
**结论**: ✅ **可行，但需要修改和测试**

---

## 📊 当前状态分析

### ✅ 已具备的基础

| 项目 | 状态 | 说明 |
|------|------|------|
| **硬件共存支持** | ✅ 已启用 | ESP32-S3 支持 WiFi/BLE 共存 |
| **软件共存配置** | ✅ 已启用 | `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y` |
| **BLE 控制器** | ✅ 已初始化 | 在 WiFi 之前初始化（符合共存要求）|
| **内存优化** | ✅ 已完成 | BLE→PSRAM(~25KB), WiFi 缓冲区优化(~22KB) |
| **BLE 配网框架** | ✅ 已实现 | `BLEWiFiProvisioner` 完整实现 |
| **共存文档** | ✅ 已有 | `docs/ble-always-on.md` 详细说明 |

### ⚠️ 当前限制（需要修改）

**代码位置**: `main/boards/common/wifi_board.cc:252-294`

```cpp
// ====== WiFi连接成功后，完全停止 BLE 协议栈以确保稳定性 (Coexistence) ======
// ⚠️ CRITICAL: 在 ESP32-S3 上，WiFi 和 BLE 共存可能导致定时器冲突崩溃。
```

**当前行为**: WiFi 连接成功后，强制停止 BLE 协议栈
- 停止 BLE WiFi Provisioner
- 停止 NimBLE 协议栈
- 禁用 BLE 控制器

**代码位置 2**: `main/main.cc:66-95`

```cpp
// ⚠️ CRITICAL: Double-check and force disable BLE controller if WiFi is connected
// This is a failsafe to prevent coexistence crashes (StoreProhibited in timer_insert)
```

**Failsafe 机制**: 在应用启动 2 秒后，强制检查并禁用 BLE 控制器

---

## 🐛 已知的共存问题

### 1. 定时器冲突崩溃

**错误**: `StoreProhibited in timer_insert`

**原因**: 
- WiFi 和 BLE 共存时，底层定时器管理冲突
- 不正确的停止顺序会导致内部定时器状态不一致

**已有解决方案**:
- 必须按正确顺序停止 BLE：Provisioner → NimBLE Stack → Controller
- 当前代码已实现此逻辑（但用于禁用 BLE，而非共存）

### 2. WiFi Power Save 冲突

**错误**: `rwble.c:508 assert`

**原因**: 
- WiFi 进入睡眠模式时，BLE 需要射频资源
- 控制器无法正确协调射频使用

**已有解决方案**:
- 禁用 WiFi Power Save 模式：`esp_wifi_set_ps(WIFI_PS_NONE)`
- 代码已实现（`ble_wifi_provisioner.cc:118-131` 和 `wifi_board.cc:284`）

### 3. 内存压力

**问题**: BLE/WiFi 共存需要大量内部 SRAM

**已有解决方案**:
- BLE 内存移至 PSRAM（节省 ~25KB）
- WiFi 缓冲区优化（节省 ~22KB）
- 总计释放约 47KB 内部 SRAM

---

## ✅ 实现方案

### 方案：启用 BLE 常驻模式

项目中已有完整的实现方案（参考 `docs/ble-always-on.md`），只是当前被注释掉了。

#### 步骤 1：移除 WiFi 成功后的 BLE 禁用逻辑

**文件**: `main/boards/common/wifi_board.cc`

**修改位置**: `StartNetwork()` 函数（第 252-294 行）

**操作**: 注释或删除以下代码块

```cpp
// 原代码（需要注释）：
/*
// ====== WiFi连接成功后，完全停止 BLE 协议栈以确保稳定性 (Coexistence) ======
auto& provisioner = BLEWiFiProvisioner::GetInstance();
provisioner.Stop();
auto& ble_service = BluetoothService::GetInstance();
ble_service.Deinitialize();
#ifdef CONFIG_BT_ENABLED
vTaskDelay(pdMS_TO_TICKS(50));
esp_err_t err = esp_bt_controller_disable();
...
#endif
*/

// 替换为（启用 BLE 常驻）：
// ====== WiFi连接成功后，启动 BLE 服务（常驻模式）======
ESP_LOGI(TAG, "========================================");
ESP_LOGI(TAG, "WiFi连接成功，启动BLE服务（常驻模式）...");
ESP_LOGI(TAG, "========================================");

// 禁用 WiFi Power Save（关键！防止 BLE 冲突）
wifi_station.SetPowerSaveMode(false);
ESP_LOGI(TAG, "✅ WiFi Power Save 已禁用");

auto& provisioner = BLEWiFiProvisioner::GetInstance();

// 设置配网成功回调（正常模式下不重启）
provisioner.SetProvisionSuccessCallback([](const std::string& ssid, const std::string& password) {
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ✅ BLE WiFi配置更新成功              ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "新SSID: %s", ssid.c_str());
    ESP_LOGW(TAG, "⚠️  WiFi配置已更新，需要重启后生效");
    // 注意：不自动重启，让用户决定何时重启
});

provisioner.SetProvisionFailureCallback([](const std::string& error_message) {
    ESP_LOGE(TAG, "❌ BLE WiFi配置失败: %s", error_message.c_str());
});

// 初始化并启动 BLE 服务
if (provisioner.Initialize("ESP32-PLAUD")) {
    if (provisioner.Start()) {
        ESP_LOGI(TAG, "✅ BLE服务已启动（常驻模式）");
        ESP_LOGI(TAG, "✓ BLE可用于设备配置和管理");
    } else {
        ESP_LOGW(TAG, "⚠️  BLE服务启动失败（不影响WiFi功能）");
    }
} else {
    ESP_LOGW(TAG, "⚠️  BLE服务初始化失败（不影响WiFi功能）");
}

ESP_LOGI(TAG, "========================================");
```

#### 步骤 2：移除或修改 Failsafe 机制

**文件**: `main/main.cc`

**修改位置**: 第 66-95 行

**选项 A：完全禁用 Failsafe**（推荐测试时使用）

```cpp
// 注释掉整个 failsafe 代码块
/*
ESP_LOGI(TAG, "========================================");
ESP_LOGI(TAG, "🔒 Failsafe: Ensuring BLE Controller is Disabled");
...
ESP_LOGI(TAG, "========================================");
*/
```

**选项 B：改为监控模式**（推荐生产环境）

```cpp
// 改为仅监控，不禁用
ESP_LOGI(TAG, "========================================");
ESP_LOGI(TAG, "🔍 监控: 检查 BLE 控制器状态");
ESP_LOGI(TAG, "========================================");

vTaskDelay(pdMS_TO_TICKS(2000));

#ifdef CONFIG_BT_ENABLED
if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    ESP_LOGI(TAG, "✅ BLE 控制器运行中（常驻模式）");
    ESP_LOGI(TAG, "   - 可通过 BLE 配置设备");
} else {
    ESP_LOGI(TAG, "⚠️  BLE 控制器未运行");
}
#endif
ESP_LOGI(TAG, "========================================");
```

#### 步骤 3：确保 WiFi Power Save 始终禁用

**文件**: `components/esp-wifi-connect/wifi_station.cc`

**检查**: `Start()` 函数中确保有以下代码

```cpp
// 在 WiFi 启动后立即禁用 Power Save
ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
ESP_LOGI("WifiStation", "✅ WiFi Power Save 已禁用（BLE共存要求）");
```

---

## 🔬 测试计划

### 阶段 1：基础功能测试（30 分钟）

#### 测试 1.1：WiFi 配网 + BLE 启动

```bash
# 1. 清除 WiFi 配置
idf.py menuconfig  # 或使用 BLE 命令清除

# 2. 重启设备
idf.py monitor

# 3. 观察日志
#    ✅ 进入配网模式
#    ✅ BLE 广播启动
#    ✅ 通过 BLE 配网成功
#    ✅ WiFi 连接成功
#    ✅ BLE 服务继续运行（新行为）
```

**预期日志**:
```
I (xxx) WifiBoard: WiFi连接成功，启动BLE服务（常驻模式）...
I (xxx) BluetoothService: BLE广播已启动 ✅
I (xxx) WifiBoard: ✅ BLE服务已启动（常驻模式）
```

#### 测试 1.2：手机连接 BLE

```bash
# 使用手机 BLE 调试工具
1. 扫描 BLE 设备
2. 连接到 "ESP32-PLAUD"
3. 发送命令测试
   {"cmd":"get_device_info"}
   {"cmd":"scan_wifi"}
   {"cmd":"get_saved_wifi"}

# ✅ 验证所有命令正常响应
```

#### 测试 1.3：稳定性测试（5 分钟）

```bash
# 观察日志，确认没有：
❌ assert failed
❌ StoreProhibited
❌ 定时器崩溃
❌ WiFi 断连
❌ BLE 断连

# ✅ 设备持续稳定运行
```

### 阶段 2：压力测试（1 小时）

#### 测试 2.1：长时间运行

- WiFi 保持连接
- BLE 保持广播
- 监控内存使用（`esp_get_free_heap_size()`）
- 监控系统稳定性

#### 测试 2.2：频繁 BLE 操作

- 连接/断开 BLE（10 次）
- 发送大量命令（WiFi 扫描、配置）
- 观察是否有内存泄漏或崩溃

#### 测试 2.3：WiFi 网络切换

```json
// 通过 BLE 切换到新 WiFi
{"cmd":"wifi_config","data":{"ssid":"NewWiFi","password":"pass"}}

// ✅ 验证：
// - 配置保存成功
// - 重启后连接新 WiFi
// - BLE 继续工作
```

### 阶段 3：性能测试（30 分钟）

#### 测试 3.1：网络性能

```bash
# 测试 WiFi 吞吐量
# 预期影响：<5-10%（可接受）
```

#### 测试 3.2：BLE 响应速度

```bash
# 测试 BLE 命令响应时间
# 预期：<100ms（正常）
```

#### 测试 3.3：内存使用

```bash
# 监控内存：
# - 空闲堆内存 > 50KB（安全）
# - 无内存泄漏
```

---

## 📊 预期性能影响

### ✅ 可接受的影响

| 项目 | 影响 | 评估 |
|------|------|------|
| **WiFi 吞吐量** | -5~10% | ✅ 足够音频流传输 |
| **BLE 响应延迟** | +10~50ms | ✅ 对配网无影响 |
| **内存占用** | +50KB | ✅ 已优化（BLE→PSRAM）|
| **功耗** | +10~20mA | ✅ BLE 广播正常功耗 |

### ❌ 需要监控的风险

| 风险 | 可能性 | 缓解措施 |
|------|-------|---------|
| **定时器崩溃** | 中等 | 已禁用 WiFi PS，正确顺序停止 |
| **内存不足** | 低 | 已优化 47KB，监控堆内存 |
| **射频冲突** | 低 | 硬件/软件共存已启用 |
| **系统不稳定** | 低 | 充分测试，监控运行状态 |

---

## 🎯 推荐实施方案

### 方案 A：完全启用（推荐）

**适用场景**: 需要持续的 BLE 配置能力

**优势**:
- ✅ 用户体验最好
- ✅ BLE 随时可用
- ✅ 灵活性最高

**风险**:
- ⚠️  需要充分测试
- ⚠️  功耗略增

**实施步骤**:
1. 应用上述修改
2. 进行完整测试
3. 监控稳定性
4. 根据测试结果调整

### 方案 B：按需启用（保守）

**适用场景**: 希望最大稳定性

**优势**:
- ✅ 风险最低
- ✅ 功耗更低

**实施**:
- 仅在需要配置时启用 BLE
- 配置完成后禁用 BLE
- 添加手动触发机制（如按键）

### 方案 C：定时启用（折中）

**适用场景**: 平衡稳定性和可用性

**优势**:
- ✅ 降低功耗
- ✅ 降低崩溃风险
- ✅ 仍然可用

**实施**:
- 每 N 分钟启用 BLE（如 5 分钟）
- 保持广播 1 分钟
- 无连接则自动禁用

---

## 💡 额外建议

### 1. 添加配置选项

```cpp
// sdkconfig 中添加
CONFIG_XIAOZHI_BLE_ALWAYS_ON=y  // 是否启用 BLE 常驻
CONFIG_XIAOZHI_BLE_TIMEOUT=300  // BLE 超时时间（秒，0=永久）
```

### 2. 添加内存监控

```cpp
// 在主循环中定期打印
void monitor_task(void* arg) {
    while(1) {
        size_t free_heap = esp_get_free_heap_size();
        ESP_LOGI("Monitor", "Free heap: %u bytes", free_heap);
        if (free_heap < 50000) {  // <50KB 报警
            ESP_LOGW("Monitor", "⚠️  Low memory!");
        }
        vTaskDelay(pdMS_TO_TICKS(10000));  // 每 10 秒
    }
}
```

### 3. 添加错误恢复机制

```cpp
// 如果 WiFi/BLE 共存出现问题，自动禁用 BLE
void coex_error_handler() {
    ESP_LOGE(TAG, "WiFi/BLE 共存错误，自动禁用 BLE");
    auto& provisioner = BLEWiFiProvisioner::GetInstance();
    provisioner.Stop();
    // ... 禁用 BLE 控制器
}
```

### 4. 添加 BLE 安全机制

```cpp
// 防止未授权访问
// - 添加 BLE 配对码
// - 添加命令白名单
// - 限制配置操作频率
```

---

## 📝 实施检查清单

### 代码修改

- [ ] 修改 `wifi_board.cc:StartNetwork()` - 启用 BLE 常驻
- [ ] 修改 `main.cc` failsafe - 改为监控模式
- [ ] 确认 WiFi Power Save 已禁用
- [ ] 添加内存监控（可选）
- [ ] 添加错误恢复机制（可选）

### 配置检查

- [ ] 确认 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y`
- [ ] 确认 `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y`
- [ ] 确认 WiFi 缓冲区已优化
- [ ] 确认内部 SRAM 保留量足够（80KB）

### 测试验证

- [ ] 基础功能测试（30 分钟）
- [ ] 稳定性测试（1 小时）
- [ ] 性能测试（30 分钟）
- [ ] 内存泄漏测试
- [ ] 多设备测试（不同开发板）

### 文档更新

- [ ] 更新 README - 添加 BLE 常驻说明
- [ ] 更新用户手册 - BLE 使用方法
- [ ] 创建故障排查文档 - 共存问题处理
- [ ] 记录测试结果

---

## 🎊 总结

### ✅ 结论

**WiFi/BLE 并行运行是可行的**，项目已具备所有必要的基础：
- ✅ 硬件和软件共存支持
- ✅ 内存优化已完成
- ✅ 实现框架已存在
- ✅ 详细文档已有

### 🚀 下一步

1. **先在测试设备上实施修改**
2. **进行完整的测试验证**
3. **根据测试结果调整**
4. **逐步扩展到生产环境**

### ⚠️  风险提示

- 需要充分测试，特别是长时间稳定性
- 建议先在单个设备上验证
- 保留回退方案（禁用 BLE 的代码）
- 监控用户反馈

### 📞 需要帮助？

如果在实施过程中遇到问题，可以参考：
- 📄 `docs/ble-always-on.md` - BLE 常驻模式详细说明
- 📄 `WIFI_MEMORY_FIX.md` - 内存优化指南
- 📄 `docs/ble-wifi-provisioning-spec.md` - BLE 配网协议

---

**评估日期**: 2025-12-01  
**评估人员**: AI Assistant  
**推荐方案**: 方案 A（完全启用）+ 充分测试

