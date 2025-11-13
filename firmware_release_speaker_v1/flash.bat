@echo off
REM ESP32-S3 PLAUD 蓝牙音箱固件烧录脚本 (Windows)
REM 使用方法：flash.bat [串口] [波特率]
REM 示例：flash.bat COM3 460800

setlocal

REM 默认参数
set PORT=%1
set BAUD=%2

if "%PORT%"=="" set PORT=COM3
if "%BAUD%"=="" set BAUD=460800

echo ======================================
echo ESP32-S3 PLAUD 蓝牙音箱固件烧录
echo ======================================
echo 串口: %PORT%
echo 波特率: %BAUD%
echo ======================================
echo.

REM 检查 Python
where python >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ❌ 错误：未找到 Python
    echo 请先安装 Python 3
    pause
    exit /b 1
)

REM 检查文件
if not exist bootloader.bin (
    echo ❌ 错误：bootloader.bin 不存在
    pause
    exit /b 1
)
if not exist partition-table.bin (
    echo ❌ 错误：partition-table.bin 不存在
    pause
    exit /b 1
)
if not exist ota_data_initial.bin (
    echo ❌ 错误：ota_data_initial.bin 不存在
    pause
    exit /b 1
)
if not exist xiaozhi.bin (
    echo ❌ 错误：xiaozhi.bin 不存在
    pause
    exit /b 1
)
if not exist generated_assets.bin (
    echo ❌ 错误：generated_assets.bin 不存在
    pause
    exit /b 1
)

echo ✅ 所有文件检查通过
echo.
echo 开始烧录...
echo.

REM 执行烧录
python -m esptool --chip esp32s3 -p %PORT% -b %BAUD% ^
  --before default_reset --after hard_reset write_flash ^
  --flash_mode dio --flash_size 16MB --flash_freq 80m ^
  0x0 bootloader.bin ^
  0x8000 partition-table.bin ^
  0xd000 ota_data_initial.bin ^
  0x20000 xiaozhi.bin ^
  0xa20000 generated_assets.bin

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ======================================
    echo ✅ 烧录成功！
    echo ======================================
    echo.
    echo 下一步：
    echo 1. 设备将自动重启
    echo 2. 可使用串口监控查看启动日志：
    echo    python -m esptool -p %PORT% monitor
    echo.
    echo 预期看到：
    echo   - I2C设备: 0x18/0x30 (ES8311), 0x40 (ES7210)
    echo   - ES8311: Codec initialized successfully
    echo   - ES7210: Codec initialized successfully
    echo   - 蓝牙: ESP32-PLAUD
    echo ======================================
) else (
    echo.
    echo ======================================
    echo ❌ 烧录失败！
    echo ======================================
    echo.
    echo 常见问题：
    echo 1. 串口被占用：
    echo    - 关闭其他串口监控程序
    echo    - 检查串口号是否正确（设备管理器）
    echo.
    echo 2. 无法连接设备：
    echo    - 按住 Boot 键，然后插入 USB
    echo    - 检查 USB 线缆是否正常
    echo    - 尝试降低波特率：flash.bat %PORT% 115200
    echo.
    echo 3. 驱动问题：
    echo    - 安装 CP210x 或 CH340 USB转串口驱动
    echo ======================================
    pause
    exit /b 1
)

pause

