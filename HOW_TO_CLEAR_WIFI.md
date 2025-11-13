# 如何清除WiFi配置

## 🎯 三种清除WiFi配置的方法

### 方法1: 通过BLE命令（推荐）⭐

使用手机App或BLE调试工具发送以下命令：

```json
{"cmd":"clear_wifi"}
```

设备会：
1. 清除所有WiFi凭证
2. 清除WiFi相关设置
3. 在500ms后自动重启
4. 重启后进入WiFi配置模式

**适用场景：**
- 正常使用时想重置WiFi
- 手机App开发测试
- 不需要连接电脑

---

### 方法2: 擦除整个Flash（最彻底）

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32
idf.py erase-flash
```

然后重新烧录固件：

```bash
idf.py flash monitor
```

**适用场景：**
- 固件出现严重问题
- 想完全清空设备数据
- 开发调试时需要从头开始

**⚠️ 警告：** 此操作会删除所有数据，包括：
- WiFi配置
- 用户设置
- 所有NVS存储的数据

---

### 方法3: 只清除NVS中的WiFi配置

#### 方式A: 使用parttool.py

```bash
cd /Users/xionghao/Documents/plaud/GitHub/xiaozhi-esp32

# 查看NVS分区内容
python $IDF_PATH/components/partition_table/parttool.py -p /dev/cu.usbserial-* read_partition --partition-name=nvs --output=nvs_backup.bin

# 擦除NVS分区
python $IDF_PATH/components/partition_table/parttool.py -p /dev/cu.usbserial-* erase_partition --partition-name=nvs
```

然后重启设备：
- 按一下RESET按钮
- 或断电重新上电

#### 方式B: 使用esptool.py

```bash
# 找到NVS分区的地址和大小（查看partition_table）
cat build/partition_table/partition-table.csv

# 假设NVS在0x9000，大小0x6000
esptool.py --chip esp32s3 --port /dev/cu.usbserial-* erase_region 0x9000 0x6000
```

**适用场景：**
- 只想清除WiFi配置，保留其他设置
- 开发调试时
- 不想重新烧录固件

---

### 方法4: 通过BLE断开WiFi（不删除配置）

如果只是想断开当前WiFi连接，但不删除配置：

```json
{"cmd":"disconnect_wifi"}
```

设备会：
1. 断开当前WiFi连接
2. WiFi配置仍保存在设备中
3. 设备不会重启

之后可以：
- 重新连接到同一WiFi
- 选择连接其他WiFi

---

## 🔍 如何验证WiFi已清除？

### 通过串口日志

连接串口后，重启设备，观察日志：

```
I (xxx) WifiBoard: 没有保存的WiFi配置
I (xxx) WifiBoard: 进入WiFi配置模式
I (xxx) WifiConfigurationAp: 启动AP: Xiaozhi-XXXX
```

看到以上日志说明WiFi配置已清除。

### 通过BLE命令

发送命令查看已保存的WiFi：

```json
{"cmd":"get_saved_wifi"}
```

如果响应中 `count` 为 0，说明WiFi配置已清除：

```json
{
  "cmd": "get_saved_wifi",
  "status": "success",
  "data": {
    "count": 0,
    "ssids": []
  }
}
```

---

## 📱 手机App集成建议

在App的设置界面添加以下功能：

### 1. "断开WiFi"按钮
```swift
func disconnectWiFi() {
    let command = """
    {"cmd":"disconnect_wifi"}
    """
    bleService.send(command)
}
```

### 2. "清除WiFi配置"按钮（危险操作）
```swift
func clearAllWiFi() {
    let alert = UIAlertController(
        title: "清除WiFi配置",
        message: "此操作会删除所有已保存的WiFi，设备将重启。确定继续吗？",
        preferredStyle: .alert
    )
    
    alert.addAction(UIAlertAction(title: "取消", style: .cancel))
    alert.addAction(UIAlertAction(title: "清除", style: .destructive) { _ in
        let command = """
        {"cmd":"clear_wifi"}
        """
        self.bleService.send(command)
        
        // 显示提示
        self.showAlert("设备正在重启...", "请稍候重新连接")
    })
    
    present(alert, animated: true)
}
```

### 3. WiFi管理界面示例

```
┌────────────────────────────────┐
│  WiFi 设置                      │
├────────────────────────────────┤
│                                │
│  当前连接                       │
│  ✓ Home-WiFi      [-45 dBm]   │
│  IP: 192.168.1.100            │
│  [断开连接]                    │
│                                │
├────────────────────────────────┤
│                                │
│  已保存的WiFi                   │
│  • Office-WiFi      [删除]     │
│  • Guest-WiFi       [删除]     │
│                                │
│  [扫描新WiFi]                  │
│                                │
├────────────────────────────────┤
│                                │
│  高级选项                       │
│  [清除所有WiFi配置]  ⚠️        │
│                                │
└────────────────────────────────┘
```

---

## 🧪 测试流程

### 测试场景1: 首次配网
```bash
1. 清除WiFi配置（方法1或2）
2. 设备重启，进入配网模式
3. 手机连接BLE
4. 发送 scan_wifi 命令
5. 选择WiFi并配置
6. 验证连接成功
```

### 测试场景2: 切换WiFi
```bash
1. 设备已连接到WiFi A
2. 手机发送 scan_wifi，看到A被标记为connected
3. 手机发送 wifi_config 连接到WiFi B
4. 设备自动断开A，连接到B
5. 验证连接成功
```

### 测试场景3: 断开并重连
```bash
1. 设备已连接到WiFi
2. 手机发送 disconnect_wifi
3. 验证设备已断开
4. 手机发送 wifi_config 重新连接
5. 验证连接成功
```

---

## 🐛 常见问题

### Q: 清除WiFi后设备没有进入配网模式？

A: 检查以下几点：
1. 确认设备已重启（观察串口日志）
2. 检查 `main/boards/common/wifi_board.cc` 中的 `StartNetwork()` 逻辑
3. 使用 `idf.py erase-flash` 完全清除后重试

### Q: BLE发送 clear_wifi 命令后没有响应？

A: 这是正常的，因为：
1. 设备会在500ms后重启
2. 重启过程中BLE连接会断开
3. 手机App应该检测到BLE断开事件

### Q: 想保留WiFi配置但断开连接？

A: 使用 `disconnect_wifi` 命令：
```json
{"cmd":"disconnect_wifi"}
```

这只会断开连接，不会删除配置。

### Q: 误删了WiFi配置怎么办？

A: 重新配置即可：
1. 设备进入配网模式
2. 手机连接BLE
3. 扫描并重新配置WiFi

---

## 📝 开发者笔记

### WiFi配置存储位置

WiFi配置存储在NVS（Non-Volatile Storage）中：

- **命名空间**: `wifi` 和 `ssid_list`
- **存储内容**: 
  - SSID列表
  - WiFi密码（加密存储）
  - WiFi相关设置

### 清除WiFi的代码实现

参见文件：`main/clear_wifi_helper.h`

```cpp
void ClearAllWiFiConfig() {
    // 1. 清除所有WiFi凭证
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.Clear();
    
    // 2. 清除WiFi相关设置
    Settings settings("wifi", true);
    settings.EraseAll();
    
    // 3. 延迟后重启
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}
```

---

**最后更新**: 2025-11-13  
**版本**: v2.0

