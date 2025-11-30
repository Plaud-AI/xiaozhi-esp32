# BLE 常驻模式 - 正常工作时保持BLE广播

## 📋 需求

用户需要在正常工作模式（WiFi已连接）下也保持BLE广播，用于：
- 功能配置
- 参数设置
- 状态查询
- 其他BLE相关功能

之前的行为：
- ❌ WiFi配网成功 → 设备重启 → 进入正常模式 → BLE不启动
- ❌ 用户无法通过BLE访问设备

## 🔧 解决方案

### 修改1：正常模式下启动BLE服务

**文件：** `main/boards/common/wifi_board.cc`

**位置：** `WifiBoard::StartNetwork()` 函数

**修改内容：**

```cpp
void WifiBoard::StartNetwork() {
    // ... WiFi连接逻辑 ...
    
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        // WiFi连接失败 → 进入配网模式
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }
    
    // ====== 新增：WiFi连接成功后，启动BLE服务 ======
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "WiFi连接成功，启动BLE服务...");
    ESP_LOGI(TAG, "========================================");
    
    auto& provisioner = BLEWiFiProvisioner::GetInstance();
    
    // 在正常模式下，配网成功回调不需要重启设备
    provisioner.SetProvisionSuccessCallback([](const std::string& ssid, const std::string& password) {
        ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║   ✅ BLE WiFi配网成功（已更新WiFi）    ║");
        ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
        ESP_LOGI(TAG, "新SSID: %s", ssid.c_str());
        ESP_LOGW(TAG, "⚠️  WiFi配置已更新，重启后生效");
        // 注意：不自动重启，让用户决定何时重启
    });
    
    provisioner.SetProvisionFailureCallback([](const std::string& error_message) {
        ESP_LOGE(TAG, "╔════════════════════════════════════════╗");
        ESP_LOGE(TAG, "║   ❌ BLE WiFi配置失败                  ║");
        ESP_LOGE(TAG, "╚════════════════════════════════════════╝");
        ESP_LOGE(TAG, "错误: %s", error_message.c_str());
    });
    
    if (provisioner.Initialize("ESP32-PLAUD")) {
        if (provisioner.Start()) {
            ESP_LOGI(TAG, "✓ BLE服务已启动（正常模式）");
            ESP_LOGI(TAG, "✓ BLE可用于功能配置和设置");
        } else {
            ESP_LOGW(TAG, "⚠️  BLE服务启动失败（不影响正常功能）");
        }
    } else {
        ESP_LOGW(TAG, "⚠️  BLE服务初始化失败（不影响正常功能）");
    }
    
    ESP_LOGI(TAG, "========================================");
}
```

### 修改2：配网模式下保持自动重启

**文件：** `main/boards/common/wifi_board.cc`

**位置：** `EnterWifiConfigMode()` 函数中的BLE配网回调

**修改内容：**

```cpp
// 设置配网成功回调
provisioner.SetProvisionSuccessCallback([](const std::string& ssid, const std::string& password) {
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║   ✅ BLE WiFi配网成功！                ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    ESP_LOGI(TAG, "SSID: %s", ssid.c_str());
    ESP_LOGI(TAG, "设备将在2秒后重启...");
    
    // 配网模式下，配网成功后自动重启设备
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
});
```

### 修改3：移除BLE配网成功后的无条件重启

**文件：** `main/ble_wifi_provisioner.cc`

**位置：** `HandleWiFiConfigCommand()` 函数

**修改内容：**

```cpp
// 调用成功回调
if (provision_success_callback_) {
    provision_success_callback_(ssid, password);
}

// 注意：是否重启由回调函数决定
// 在配网模式下，回调会调用 esp_restart()
// 在正常模式下，回调只记录信息，不重启
```

**旧代码（已移除）：**
```cpp
// 延迟后重启设备
ESP_LOGI(TAG, "将在2秒后重启设备...");
vTaskDelay(pdMS_TO_TICKS(2000));
esp_restart();
```

---

## 📊 工作流程对比

### 旧流程（修改前）

```
设备启动
  ↓
读取WiFi配置
  ↓
有配置？
  ├─ 无 → 进入配网模式 → BLE广播 ✅
  └─ 有 → 连接WiFi
         ↓
      连接成功 → 正常工作模式 → BLE不启动 ❌
         ↓
      连接失败 → 进入配网模式 → BLE广播 ✅
```

### 新流程（修改后）

```
设备启动
  ↓
读取WiFi配置
  ↓
有配置？
  ├─ 无 → 进入配网模式 → BLE广播 ✅
  └─ 有 → 连接WiFi
         ↓
      连接成功 → 正常工作模式 → 启动BLE服务 ✅
         ↓
      连接失败 → 进入配网模式 → BLE广播 ✅
```

**关键变化：正常工作模式下也启动BLE服务！**

---

## 🎯 两种模式的行为差异

### 1️⃣ 配网模式（WiFi未配置/连接失败）

**BLE行为：**
- ✅ BLE广播
- ✅ 可以进行WiFi配网
- ✅ 配网成功后自动重启

**配网成功流程：**
```
用户通过BLE配网 → 输入WiFi信息
                  ↓
设备连接WiFi → 成功 ✅
                  ↓
保存WiFi凭证
                  ↓
发送成功响应给手机
                  ↓
等待2秒 → 自动重启 🔄
                  ↓
重启后进入正常模式
```

### 2️⃣ 正常模式（WiFi已连接）

**BLE行为：**
- ✅ BLE广播
- ✅ 可以进行功能配置
- ✅ 可以更新WiFi配置（但不自动重启）

**WiFi配置更新流程：**
```
用户通过BLE配置新WiFi → 输入新WiFi信息
                       ↓
设备尝试连接新WiFi → 成功 ✅
                       ↓
保存新WiFi凭证
                       ↓
发送成功响应给手机
                       ↓
⚠️  WiFi配置已更新，重启后生效
                       ↓
设备继续运行（不自动重启） ✅
                       ↓
用户可以：
 • 继续使用当前WiFi
 • 手动重启以切换到新WiFi
 • 通过其他方式重启设备
```

---

## 📝 预期日志

### 首次启动（无WiFi配置）

```
I (239) main: BLE配网服务将在需要时启动
...
I (61479) WifiStation: Wait for next scan  ← 无配置，等待超时
I (61489) WifiBoard: 进入配网模式
I (61639) WifiBoard: 第1步: 启动 Soft AP WiFi 配网
...
I (76769) WifiBoard: 第2步: 启动 BLE WiFi 配网
I (76939) BluetoothService: BLE广播已启动 ✅
```

### 配网成功后重启

```
I (xxxxx) BLEWiFiProvisioner: ✅ WiFi连接成功！
I (xxxxx) WifiBoard: SSID: YourWiFi
I (xxxxx) WifiBoard: 设备将在2秒后重启...
```

### 重启后（正常模式）

```
I (1369) wifi: WiFi STA启动
I (5000) WifiStation: Connected to YourWiFi ✅
I (5010) WifiBoard: ========================================
I (5010) WifiBoard: WiFi连接成功，启动BLE服务...
I (5010) WifiBoard: ========================================
I (5020) BLEWiFiProvisioner: 初始化 BLE WiFi Provisioner
I (5030) BluetoothService: BLE服务初始化成功
I (5040) BluetoothService: BLE广播已启动 ✅
I (5050) WifiBoard: ✓ BLE服务已启动（正常模式）
I (5060) WifiBoard: ✓ BLE可用于功能配置和设置
I (5070) WifiBoard: ========================================
```

### 正常模式下更新WiFi配置

```
I (xxxxx) BLEWiFiProvisioner: 收到WiFi配置命令
I (xxxxx) BLEWiFiProvisioner: 新SSID: NewWiFi
I (xxxxx) BLEWiFiProvisioner: ✅ WiFi连接成功！
I (xxxxx) WifiBoard: ╔════════════════════════════════════════╗
I (xxxxx) WifiBoard: ║   ✅ BLE WiFi配网成功（已更新WiFi）    ║
I (xxxxx) WifiBoard: ╚════════════════════════════════════════╝
I (xxxxx) WifiBoard: 新SSID: NewWiFi
I (xxxxx) WifiBoard: ⚠️  WiFi配置已更新，重启后生效
← 设备继续运行，不重启 ✅
```

---

## ✅ 功能验证

### 测试步骤

1. **首次配网：**
   - 清除WiFi配置
   - 设备重启
   - 确认BLE可见 ✅
   - 通过BLE配网WiFi
   - 确认配网成功后自动重启 ✅

2. **正常模式：**
   - 设备重启后自动连接WiFi ✅
   - 确认BLE仍然可见 ✅
   - 手机可以连接BLE ✅

3. **正常模式下更新WiFi：**
   - 通过BLE发送新WiFi配置
   - 确认配置保存成功 ✅
   - 确认设备不自动重启 ✅
   - 手动重启设备
   - 确认连接到新WiFi ✅

4. **BLE功能测试：**
   - 在正常模式下连接BLE
   - 测试WiFi扫描功能 ✅
   - 测试设备信息查询 ✅
   - 测试其他自定义功能 ✅

---

## 🎉 总结

### ✅ 修改内容

1. **正常模式下启动BLE服务** - 保持BLE常驻
2. **配网模式保持自动重启** - 首次配网后清理状态
3. **正常模式不自动重启** - 更新WiFi配置时让用户决定

### ✅ 优势

1. **用户体验更好** - BLE始终可用，随时可以配置
2. **灵活性更高** - 正常模式下更新WiFi不打断工作
3. **功能扩展性强** - BLE可用于更多功能，不仅仅是配网

### ⚠️  注意事项

1. **内存占用** - BLE服务会占用一定内存（约50KB）
2. **功耗** - BLE广播会增加功耗（约10-20mA）
3. **安全性** - 建议添加BLE配对/加密机制（后续优化）

### 💡 后续优化建议

1. **可选BLE** - 添加配置选项，允许用户关闭BLE
2. **BLE安全** - 添加配对码或密码保护
3. **功能分离** - 将BLE配网和BLE功能配置分开（不同的GATT服务）
4. **动态广播** - 根据设备状态调整BLE广播间隔（节能）


