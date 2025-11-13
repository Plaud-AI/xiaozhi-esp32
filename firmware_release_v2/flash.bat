@echo off
REM ESP32-S3 PLAUD 蓝牙音箱固件烧录脚本 (Windows)
REM 版本：v2.0 (修复版)

setlocal

set PORT=%1
set BAUD=%2

if "%PORT%"=="" set PORT=COM3
if "%BAUD%"=="" set BAUD=460800

echo ======================================
echo ESP32-S3 PLAUD 固件烧录 v2.0
echo ======================================
echo 固件版本: v2.0 (ES8311修复版)
echo 编译时间: 2025-11-13 17:15
echo 串口: %PORT%
echo 波特率: %BAUD%
echo ======================================
echo.
echo ⚠️  重要提示：
echo    - Flash模式: DIO (不是QIO)
echo    - ES8311 地址已更新为 0x18
echo    - ES7210 未焊接时不会崩溃
echo.
echo ======================================
echo.

REM 检查 Python
where python >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ❌ 错误：未找到 Python
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

REM 烧录
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
    echo 预期启动日志：
    echo   ✅ I2C扫描: 0x18 (ES8311)
    echo   ✅ ES8311: Codec initialized successfully
    echo   ⚠️  ES7210: 未焊接警告（正常）
    echo   ✅ Application: STATE: running
    echo.
    echo 监控串口：
    echo   python -m esptool -p %PORT% monitor
    echo ======================================
) else (
    echo.
    echo ======================================
    echo ❌ 烧录失败！
    echo ======================================
    echo.
    echo 请检查：
    echo   1. 串口号是否正确
    echo   2. 是否按住Boot键
    echo   3. USB线缆是否正常
    echo ======================================
    pause
    exit /b 1
)

pause

