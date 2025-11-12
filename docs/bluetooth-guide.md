# 蓝牙功能使用指南

## 概述

XiaoZhi-ESP32 现已集成低功耗蓝牙（BLE）功能，允许手机通过蓝牙扫描、连接并与设备进行数据通信。

## 设备信息

### 蓝牙广播信息
- **设备名称**: `XiaoZhi-AI`
- **协议**: BLE (Bluetooth Low Energy)
- **协议栈**: NimBLE

### GATT服务信息
- **服务UUID**: `0000FFE0-0000-1000-8000-00805F9B34FB`
- **特征UUID**: `0000FFE1-0000-1000-8000-00805F9B34FB`
- **特征属性**: 
  - 可读 (READ)
  - 可写 (WRITE)
  - 可通知 (NOTIFY)

## 如何连接

### 方法一：使用专业蓝牙调试工具（推荐）

#### 推荐的App:
1. **nRF Connect** (Android/iOS)
   - Google Play / App Store 搜索 "nRF Connect"
   - Nordic官方出品，功能强大

2. **LightBlue** (iOS) / **BLE Scanner** (Android)
   - 界面友好，易于使用

#### 连接步骤:
1. 打开蓝牙调试App
2. 扫描周围的BLE设备
3. 找到名为 `XiaoZhi-AI` 的设备
4. 点击连接
5. 连接成功后，浏览GATT服务
6. 找到服务 UUID `FFE0`
7. 找到特征 UUID `FFE1`

### 方法二：自定义App开发

如果您正在开发自己的手机App，可以使用以下方式集成：

#### Android示例（使用Android BLE API）
```java
// 扫描BLE设备
BluetoothLeScanner scanner = bluetoothAdapter.getBluetoothLeScanner();
scanner.startScan(scanCallback);

// 连接设备
BluetoothDevice device = bluetoothAdapter.getRemoteDevice(macAddress);
BluetoothGatt gatt = device.connectGatt(context, false, gattCallback);

// 发现服务
gatt.discoverServices();

// 读写特征
UUID serviceUUID = UUID.fromString("0000FFE0-0000-1000-8000-00805F9B34FB");
UUID charUUID = UUID.fromString("0000FFE1-0000-1000-8000-00805F9B34FB");
BluetoothGattService service = gatt.getService(serviceUUID);
BluetoothGattCharacteristic characteristic = service.getCharacteristic(charUUID);

// 写入数据
characteristic.setValue("Hello XiaoZhi");
gatt.writeCharacteristic(characteristic);

// 启用通知
gatt.setCharacteristicNotification(characteristic, true);
```

#### iOS示例（使用CoreBluetooth）
```swift
import CoreBluetooth

class BLEManager: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    let serviceUUID = CBUUID(string: "0000FFE0-0000-1000-8000-00805F9B34FB")
    let characteristicUUID = CBUUID(string: "0000FFE1-0000-1000-8000-00805F9B34FB")
    
    var centralManager: CBCentralManager!
    var peripheral: CBPeripheral?
    
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            central.scanForPeripherals(withServices: nil, options: nil)
        }
    }
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                       advertisementData: [String : Any], rssi RSSI: NSNumber) {
        if peripheral.name == "XiaoZhi-AI" {
            self.peripheral = peripheral
            central.connect(peripheral, options: nil)
        }
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.delegate = self
        peripheral.discoverServices([serviceUUID])
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        for service in services {
            peripheral.discoverCharacteristics([characteristicUUID], for: service)
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                   error: Error?) {
        guard let characteristics = service.characteristics else { return }
        for characteristic in characteristics {
            // 启用通知
            peripheral.setNotifyValue(true, for: characteristic)
            
            // 写入数据
            let data = "Hello XiaoZhi".data(using: .utf8)!
            peripheral.writeValue(data, for: characteristic, type: .withResponse)
        }
    }
}
```

## 数据通信

### 发送数据到设备
1. 在蓝牙调试App中，找到特征 `FFE1`
2. 点击"Write"功能
3. 输入要发送的文本或十六进制数据
4. 点击发送

**设备会自动回复:** 收到的数据会被回显，格式为 `收到: [你发送的数据]`

### 接收设备数据
1. 在特征 `FFE1` 上启用通知（Notify）
2. 设备会主动推送数据到手机
3. 在App中可以看到接收到的数据

### 数据格式
- **编码**: UTF-8
- **最大长度**: 取决于MTU大小（默认256字节）

## MAC地址获取

设备的蓝牙MAC地址会在启动时打印到串口日志中：

```
========================================
🎉 蓝牙服务已启动
📱 设备名称: XiaoZhi-AI
📍 MAC地址: XX:XX:XX:XX:XX:XX
========================================
```

您可以通过串口监视器查看此信息：
```bash
idf.py monitor
```

## 示例交互流程

### 1. 基本通信测试
```
手机 → 设备: "Hello"
设备 → 手机: "收到: Hello"

手机 → 设备: "测试中文"
设备 → 手机: "收到: 测试中文"
```

### 2. 自定义数据处理
您可以在 `main/main.cc` 或 `main/application.cc` 中修改数据接收回调函数，实现自定义的数据处理逻辑：

```cpp
#include "bluetooth_example.h"

void app_main() {
    // ... 其他初始化代码 ...
    
    // 初始化蓝牙服务
    auto& bt = BluetoothService::GetInstance();
    
    // 设置自定义数据接收回调
    bt.SetDataReceivedCallback([](const std::string& data) {
        ESP_LOGI("MyApp", "收到蓝牙数据: %s", data.c_str());
        
        // 在这里处理接收到的数据
        if (data == "LED_ON") {
            // 打开LED
        } else if (data == "LED_OFF") {
            // 关闭LED
        }
        
        // 发送自定义回复
        BluetoothService::GetInstance().SendData("处理完成");
    });
    
    bt.Initialize("XiaoZhi-AI");
    
    // ... 启动应用 ...
}
```

## 故障排除

### 1. 手机搜索不到设备
- 确保设备已启动且蓝牙服务正常初始化
- 检查手机蓝牙是否已开启
- 尝试重启设备和手机蓝牙
- 查看串口日志确认BLE广播已启动

### 2. 连接失败
- 确保设备未被其他手机连接（最多支持1个连接）
- 尝试断开其他蓝牙连接后重试
- 重启设备

### 3. 数据发送失败
- 确保已成功连接到设备
- 检查数据长度是否超过MTU限制
- 查看串口日志的错误信息

### 4. 收不到通知
- 确保已在特征上启用通知功能
- 检查手机App是否正确注册了通知回调
- 确认设备端有调用 `SendData()` 发送数据

## 技术规格

### 蓝牙参数
- **协议版本**: Bluetooth 5.0
- **角色**: Peripheral (从设备)
- **最大连接数**: 1
- **广播间隔**: 20-40ms
- **连接间隔**: 由主设备决定
- **MTU大小**: 256字节（可配置）

### 内存占用
- **代码大小**: ~50KB
- **运行时内存**: ~20KB

## 配置选项

蓝牙功能的配置在 `sdkconfig.defaults` 中：

```ini
# 启用蓝牙
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y

# 设置为从设备角色
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y

# 最大连接数
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1

# MTU大小
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256

# 设备名称
CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="XiaoZhi-AI"
```

您可以通过 `idf.py menuconfig` 修改这些配置。

## API参考

### BluetoothService类

```cpp
class BluetoothService {
public:
    // 获取单例实例
    static BluetoothService& GetInstance();
    
    // 初始化蓝牙服务
    bool Initialize(const std::string& device_name);
    
    // 启动BLE广播
    bool StartAdvertising();
    
    // 停止BLE广播
    void StopAdvertising();
    
    // 发送数据到已连接的客户端
    bool SendData(const std::string& data);
    
    // 设置数据接收回调
    void SetDataReceivedCallback(std::function<void(const std::string&)> callback);
    
    // 获取设备名称
    std::string GetDeviceName() const;
    
    // 获取设备MAC地址
    std::string GetMacAddress() const;
    
    // 是否已连接
    bool IsConnected() const;
};
```

### 快速集成示例

在 `main/main.cc` 中添加：

```cpp
#include "bluetooth_example.h"

extern "C" void app_main(void) {
    // ... NVS初始化等 ...
    
    // 初始化蓝牙服务（一行代码搞定！）
    InitializeBluetoothService();
    
    // ... 启动应用 ...
}
```

## 安全注意事项

1. **默认无配对**: 当前实现不需要配对即可连接
2. **无加密**: 数据传输未加密
3. **生产环境建议**: 
   - 添加配对要求
   - 启用连接加密
   - 实现白名单机制

## 进阶功能（TODO）

以下功能可以在后续版本中添加：

- [ ] 配对和绑定支持
- [ ] 连接加密
- [ ] 自定义服务和特征
- [ ] OTA固件更新通过BLE
- [ ] 低功耗模式优化
- [ ] 多连接支持

## 相关文件

- `main/bluetooth_service.h` - 蓝牙服务头文件
- `main/bluetooth_service.cc` - 蓝牙服务实现
- `main/bluetooth_example.h` - 快速集成示例
- `sdkconfig.defaults` - 蓝牙配置
- `main/CMakeLists.txt` - 构建配置

## 参考资料

- [ESP-IDF NimBLE文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/bluetooth/nimble/index.html)
- [Bluetooth SIG GATT规范](https://www.bluetooth.com/specifications/specs/gatt-specification-supplement/)
- [nRF Connect App](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-mobile)

