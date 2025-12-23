/**
 * @file cs_press_driver.cc
 * @brief CHIPSEA CSU18M6X 压感传感器驱动实现
 * 
 * 基于参考工程 cs_press_m6x_driver.c 对齐
 */

#include "cs_press_driver.h"
#include "cs_press_fw_data.h"

#ifdef CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

#define TAG "CsPressDriver"

/* ============ 内部常量 ============ */
#define SOFT_RESET_ENABLE           1
#define RETRY_NUM                   2
#define DEBUG_MODE_DELAY_TIME       20

/* ============ Boot 命令 ============ */
static const uint8_t boot_fw_write_cmd[BOOT_CMD_LENGTH] = {0x55, 0xa5, 0x5a};
static const uint8_t boot_fw_read_cmd[BOOT_CMD_LENGTH] = {0x55, 0xa5, 0xa5};
static const uint8_t boot_fw_jump_cmd[BOOT_CMD_LENGTH] = {0x55, 0xa5, 0xaa};
static const uint8_t boot_fw_reset_cmd[BOOT_CMD_LENGTH] = {0x55, 0xa5, 0x55};
static const uint8_t boot_fw_wflag_cmd[BOOT_CMD_LENGTH] = {0x55, 0xa5, 0x51};

/* ============ 状态检测数据 ============ */
static const uint8_t boot_status_reg_dat[4] = {0x10, 0x01, 0x18, 0x60};
static const uint8_t boot_status_reg_dat_validbit[4] = {0xf0, 0xff, 0xff, 0xf0};
static const uint8_t ap_status_reg_dat[4] = {0x80, 0x81, 0x18, 0x60};
static const uint8_t ap_status_reg_dat_validbit[4] = {0xff, 0xff, 0xff, 0xf0};

/* ============ 构造函数 ============ */

CsPressDriver::CsPressDriver(i2c_master_bus_handle_t i2c_bus, 
                             uint8_t i2c_addr,
                             gpio_num_t rst_pin)
    : i2c_bus_(i2c_bus)
    , i2c_dev_(nullptr)
    , rst_pin_(rst_pin)
    , i2c_addr_(i2c_addr)
    , initialized_(false)
    , owns_i2c_bus_(false)
    , callback_(nullptr) {
}

CsPressDriver::CsPressDriver(i2c_port_t i2c_port,
                             gpio_num_t sda_pin,
                             gpio_num_t scl_pin,
                             gpio_num_t rst_pin)
    : i2c_bus_(nullptr)
    , i2c_dev_(nullptr)
    , rst_pin_(rst_pin)
    , i2c_addr_(CS_PRESS_I2C_ADDR)
    , initialized_(false)
    , owns_i2c_bus_(true)
    , callback_(nullptr) {
    
    // 创建 I2C 总线
    i2c_master_bus_config_t bus_config = {
        .i2c_port = i2c_port,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    
    esp_err_t ret = i2c_new_master_bus(&bus_config, &i2c_bus_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        i2c_bus_ = nullptr;
    }
}

CsPressDriver::~CsPressDriver() {
    Deinit();
}

/* ============ 初始化和反初始化 ============ */

esp_err_t CsPressDriver::Init() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (i2c_bus_ == nullptr) {
        ESP_LOGE(TAG, "I2C bus not available");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing CHIPSEA pressure sensor...");

    // 添加 I2C 设备
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr_,
        .scl_speed_hz = CS_PRESS_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &dev_config, &i2c_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置复位引脚
    if (rst_pin_ != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << rst_pin_),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(rst_pin_, 1);
    }

    // 复位 IC
    ret = ResetIc();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Reset IC failed, continue anyway");
    }

    // 更新固件（使用完整固件数据）
    ret = FwForceUpdate(cs_press_default_fw_array);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Firmware update failed");
        // 继续尝试，可能固件已经存在
    }

    initialized_ = true;
    ESP_LOGI(TAG, "CHIPSEA pressure sensor initialized");
    
    // 打印固件信息
    PrintFwInfo();
    
    return ESP_OK;
}

esp_err_t CsPressDriver::InitWithoutFwUpdate() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (i2c_bus_ == nullptr) {
        ESP_LOGE(TAG, "I2C bus not available");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing CHIPSEA pressure sensor (no FW update)...");

    // 添加 I2C 设备
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr_,
        .scl_speed_hz = CS_PRESS_I2C_FREQ_HZ,
    };

    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &dev_config, &i2c_dev_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    // 等待芯片启动
    DelayMs(500);
    
    // 唤醒设备
    ret = WakeUp();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WakeUp failed, continue anyway");
    }

    initialized_ = true;
    ESP_LOGI(TAG, "CHIPSEA pressure sensor initialized (no FW update)");
    
    // 打印固件信息
    PrintFwInfo();
    
    return ESP_OK;
}

void CsPressDriver::Deinit() {
    if (!initialized_) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing CHIPSEA pressure sensor...");

    if (i2c_dev_ != nullptr) {
        i2c_master_bus_rm_device(i2c_dev_);
        i2c_dev_ = nullptr;
    }

    if (owns_i2c_bus_ && i2c_bus_ != nullptr) {
        i2c_del_master_bus(i2c_bus_);
        i2c_bus_ = nullptr;
    }

    initialized_ = false;
}

/* ============ 底层 I2C 操作 ============ */

esp_err_t CsPressDriver::I2cWrite(uint8_t reg_addr, const uint8_t* data, size_t length) {
    if (i2c_dev_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // 创建发送缓冲区: [寄存器地址 + 数据]
    uint8_t* write_buf = new uint8_t[length + 1];
    if (write_buf == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    write_buf[0] = reg_addr;
    if (data != nullptr && length > 0) {
        memcpy(write_buf + 1, data, length);
    }

    esp_err_t ret = i2c_master_transmit(i2c_dev_, write_buf, length + 1, 100);
    delete[] write_buf;

    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C write failed, reg=0x%02X, ret=%s", reg_addr, esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t CsPressDriver::I2cRead(uint8_t reg_addr, uint8_t* data, size_t length) {
    if (i2c_dev_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = i2c_master_transmit_receive(i2c_dev_, &reg_addr, 1, data, length, 100);

    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C read failed, reg=0x%02X, ret=%s", reg_addr, esp_err_to_name(ret));
    }

    return ret;
}

/* ============ 辅助函数 ============ */

void CsPressDriver::DelayMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void CsPressDriver::PowerUp() {
    if (rst_pin_ != GPIO_NUM_NC) {
        gpio_set_level(rst_pin_, 1);
        ESP_LOGD(TAG, "Power UP (RST=1)");
    }
}

void CsPressDriver::PowerDown() {
    if (rst_pin_ != GPIO_NUM_NC) {
        gpio_set_level(rst_pin_, 0);
        ESP_LOGD(TAG, "Power DOWN (RST=0)");
    }
}

esp_err_t CsPressDriver::SoftReset() {
    uint8_t temp_data[3];

    // boot reset cmd
    temp_data[0] = 0x55;
    temp_data[1] = 0xa5;
    temp_data[2] = 0x55;
    esp_err_t ret = I2cWrite(BOOT_CMD_REG, temp_data, BOOT_CMD_LENGTH);

    DelayMs(3);

    // ap reset cmd
    temp_data[0] = AP_SOFT_RESRT_CMD;
    ret = I2cWrite(AP_RESRT_REG, temp_data, 1);

    return ret;
}

esp_err_t CsPressDriver::WakeupI2c() {
    return I2cRwTest(0x67);
}

esp_err_t CsPressDriver::CleanDebugMode() {
    uint8_t temp_data = 0;
    esp_err_t ret = I2cWrite(DEBUG_MODE_REG, &temp_data, 1);
    ret |= I2cWrite(DEBUG_READY_REG, &temp_data, 1);
    return ret;
}

esp_err_t CsPressDriver::SetDebugMode(uint8_t mode_num) {
    return I2cWrite(DEBUG_MODE_REG, &mode_num, 1);
}

esp_err_t CsPressDriver::SetDebugReady(uint8_t ready_num) {
    return I2cWrite(DEBUG_READY_REG, &ready_num, 1);
}

uint8_t CsPressDriver::GetDebugReady() {
    uint8_t ready_num = 0;
    esp_err_t ret = I2cRead(DEBUG_READY_REG, &ready_num, 1);
    if (ret != ESP_OK) {
        ready_num = 0;
    }
    return ready_num;
}

esp_err_t CsPressDriver::WriteDebugData(const uint8_t* data, uint8_t length) {
    return I2cWrite(DEBUG_DATA_REG, data, length);
}

esp_err_t CsPressDriver::ReadDebugData(uint8_t* data, uint8_t length) {
    return I2cRead(DEBUG_DATA_REG, data, length);
}

/* ============ 公开接口实现 ============ */

esp_err_t CsPressDriver::ResetIc() {
#if SOFT_RESET_ENABLE
    return SoftReset();
#else
    PowerDown();
    DelayMs(100);
    PowerUp();
    return ESP_OK;
#endif
}

esp_err_t CsPressDriver::I2cRwTest(uint8_t test_data) {
    uint8_t retry = RETRY_NUM;
    uint8_t read_data = 0;
    esp_err_t ret;

    do {
        I2cWrite(AP_RW_TEST_REG, &test_data, 1);
        I2cRead(AP_RW_TEST_REG, &read_data, 1);

        ret = ESP_OK;
        if (read_data != test_data) {
            ret = ESP_FAIL;
            DelayMs(1);
        }
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

esp_err_t CsPressDriver::WakeUp() {
    uint8_t retry = RETRY_NUM;
    uint8_t temp_data = 1;
    esp_err_t ret = ESP_OK;

    do {
        if (ret != ESP_OK) {
            DelayMs(1);
        }
        ret = I2cWrite(AP_WAKEUP_REG, &temp_data, 1);
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

esp_err_t CsPressDriver::Sleep() {
    uint8_t retry = RETRY_NUM;
    uint8_t temp_data = 1;
    esp_err_t ret = ESP_OK;

    do {
        if (ret != ESP_OK) {
            DelayMs(1);
        }
        ret = I2cWrite(AP_SLEEP_REG, &temp_data, 1);
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

esp_err_t CsPressDriver::ReadFwInfo(CsFwInfo& fw_info) {
    uint8_t read_temp[FW_ONE_BLOCK_LENGTH];
    esp_err_t ret = ESP_OK;

    WakeupI2c();

    // Manufacturer ID
    ret |= I2cRead(AP_MANUFACTURER_ID_REG, read_temp, CS_MANUFACTURER_ID_LENGTH);
    if (ret == ESP_OK) {
        fw_info.manufacturer_id = ((uint16_t)read_temp[1] << 8) | read_temp[0];
    }

    // Module ID
    ret |= I2cRead(AP_MODULE_ID_REG, read_temp, CS_MODULE_ID_LENGTH);
    if (ret == ESP_OK) {
        fw_info.module_id = ((uint16_t)read_temp[1] << 8) | read_temp[0];
    }

    // FW Version
    ret |= I2cRead(AP_VERSION_REG, read_temp, CS_FW_VERSION_LENGTH);
    if (ret == ESP_OK) {
        fw_info.fw_version = ((uint16_t)read_temp[1] << 8) | read_temp[0];
    }

    return ret;
}

void CsPressDriver::PrintFwInfo() {
    CsFwInfo fw_info;
    esp_err_t ret = ReadFwInfo(fw_info);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Firmware Info:");
        ESP_LOGI(TAG, "  Version: 0x%04X", fw_info.fw_version);
        ESP_LOGI(TAG, "  Manufacturer ID: 0x%04X", fw_info.manufacturer_id);
        ESP_LOGI(TAG, "  Module ID: 0x%04X", fw_info.module_id);
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGE(TAG, "Failed to read firmware info!");
    }
}

int CsPressDriver::CheckFwStatus() {
    uint8_t read_data[4];

    I2cRwTest(0x12);  // wake up i2c
    DelayMs(3);

    esp_err_t ret = I2cRead(FW_STATUS_REG, read_data, 4);

    if (ret == ESP_OK) {
        // check boot status
        bool is_boot = true;
        for (int i = 0; i < 4; i++) {
            if (boot_status_reg_dat[i] != (read_data[i] & boot_status_reg_dat_validbit[i])) {
                is_boot = false;
                break;
            }
        }
        if (is_boot) {
            return 0;  // boot status
        }

        // check ap status
        bool is_ap = true;
        for (int i = 0; i < 4; i++) {
            if (ap_status_reg_dat[i] != (read_data[i] & ap_status_reg_dat_validbit[i])) {
                is_ap = false;
                break;
            }
        }
        if (is_ap) {
            return 1;  // ap status
        }
    }

    return -1;  // error
}

esp_err_t CsPressDriver::ReadSensorDataInit() {
    WakeupI2c();
    CleanDebugMode();
    return SetDebugMode(AP_R_SENSOR_DATA_DEBUG_MODE);
}

esp_err_t CsPressDriver::ReadSensorData(CsSensorData& sensor_data) {
    uint8_t data_temp[13];
    uint8_t num;
    uint8_t checksum;

    num = GetDebugReady();

    if (num == 9) {
        esp_err_t ret = ReadDebugData(data_temp, 9);
        if (ret == ESP_OK) {
            checksum = 0;
            for (int i = 0; i < 8; i++) {
                checksum += data_temp[i];
            }

            if (checksum == data_temp[8]) {
                sensor_data.rawdata[0] = (int16_t)(((uint16_t)data_temp[1] << 8) | data_temp[0]);
                sensor_data.rawdata[1] = (int16_t)(((uint16_t)data_temp[3] << 8) | data_temp[2]);
                sensor_data.arith_rawdata[0] = (int16_t)(((uint16_t)data_temp[5] << 8) | data_temp[4]);
                sensor_data.arith_rawdata[1] = (int16_t)(((uint16_t)data_temp[7] << 8) | data_temp[6]);

                SetDebugReady(0);
                return ESP_OK;
            }
        }
    }

    SetDebugReady(0);
    return ESP_FAIL;
}

esp_err_t CsPressDriver::ReadPressure(uint16_t& pressure) {
    uint8_t read_temp[4] = {0};
    uint8_t checksum;

    esp_err_t ret = I2cRead(AP_FORCEDATA_REG, read_temp, 4);

    if (ret == ESP_OK) {
        if (read_temp[0] == 0x01) {
            checksum = read_temp[1] + read_temp[2];
            if (checksum == read_temp[3]) {
                pressure = ((uint16_t)read_temp[2] << 8) | read_temp[1];
                return ESP_OK;
            }
        }
    }

    return ESP_FAIL;
}

esp_err_t CsPressDriver::ReadPressureAvg(uint16_t& pressure) {
    // 简单实现：多次读取取平均
    uint32_t sum = 0;
    int count = 0;
    
    for (int i = 0; i < 5; i++) {
        uint16_t p;
        if (ReadPressure(p) == ESP_OK) {
            sum += p;
            count++;
        }
        DelayMs(10);
    }
    
    if (count > 0) {
        pressure = (uint16_t)(sum / count);
        return ESP_OK;
    }
    
    return ESP_FAIL;
}

esp_err_t CsPressDriver::CalibrationEnable() {
    uint8_t retry = RETRY_NUM;
    uint8_t temp_data = 1;
    esp_err_t ret = ESP_OK;

    do {
        if (ret != ESP_OK) {
            DelayMs(1);
        }
        ret = I2cWrite(AP_CALIBRATION_REG, &temp_data, 1);
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

esp_err_t CsPressDriver::CalibrationDisable() {
    uint8_t retry = RETRY_NUM;
    uint8_t temp_data = 0;
    esp_err_t ret = ESP_OK;

    do {
        if (ret != ESP_OK) {
            DelayMs(1);
        }
        ret = I2cWrite(AP_CALIBRATION_REG, &temp_data, 1);
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

int CsPressDriver::CalibrationCheck(CsCalibrationResult& result) {
    uint8_t num;
    uint8_t data_temp[9];

    SetDebugMode(AP_CALIBRATION_DEBUG_MODE);

    num = GetDebugReady();

    if (num == 9) {
        esp_err_t ret = ReadDebugData(data_temp, 9);

        if (ret == ESP_OK) {
            result.calibration_progress = data_temp[0];
            result.calibration_factor = ((uint16_t)data_temp[2] << 8) | data_temp[1];
            result.press_adc_1st = (int16_t)(((uint16_t)data_temp[4] << 8) | data_temp[3]);
            result.press_adc_2nd = (int16_t)(((uint16_t)data_temp[6] << 8) | data_temp[5]);
            result.press_adc_3rd = (int16_t)(((uint16_t)data_temp[8] << 8) | data_temp[7]);

            if (result.calibration_progress == CALIBRATION_SUCCESS_FLAG) {
                return 1;  // 完成
            }
            return 0;  // 进行中
        }
    }

    return -1;  // 错误
}

esp_err_t CsPressDriver::ReadCalibrationFactor(uint16_t& cal_factor) {
    uint8_t data_temp[4] = {0};
    uint8_t num;

    WakeupI2c();

    CleanDebugMode();
    SetDebugMode(AP_R_CAL_FACTOR_DEBUG_MODE);
    SetDebugReady(2);

    DelayMs(DEBUG_MODE_DELAY_TIME);

    num = GetDebugReady();

    if (num == 4) {
        esp_err_t ret = ReadDebugData(data_temp, 4);
        if (ret == ESP_OK) {
            cal_factor = ((uint16_t)data_temp[1] << 8) | data_temp[0];
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

esp_err_t CsPressDriver::WriteCalibrationFactor(uint16_t cal_factor) {
    uint8_t data_temp[7];
    uint16_t read_cal_factor;

    WakeupI2c();

    CleanDebugMode();
    SetDebugMode(AP_W_CAL_FACTOR_DEBUG_MODE);

    data_temp[0] = 0;
    data_temp[1] = 0;
    data_temp[2] = cal_factor & 0xFF;
    data_temp[3] = (cal_factor >> 8) & 0xFF;
    data_temp[4] = 0;
    data_temp[5] = 0;
    data_temp[6] = data_temp[0] + data_temp[1] + data_temp[2] + data_temp[3] + data_temp[4] + data_temp[5];

    WriteDebugData(data_temp, 7);
    SetDebugReady(7);

    DelayMs(DEBUG_MODE_DELAY_TIME);

    ReadCalibrationFactor(read_cal_factor);

    if (read_cal_factor != cal_factor) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t CsPressDriver::ReadPressLevel(uint16_t& press_level) {
    uint8_t data_temp[2] = {0};
    uint8_t num;

    WakeupI2c();

    CleanDebugMode();
    SetDebugMode(AP_R_PRESS_LEVEL_DEBUG_MODE);
    SetDebugReady(1);

    DelayMs(DEBUG_MODE_DELAY_TIME);

    num = GetDebugReady();

    if (num == 2) {
        esp_err_t ret = ReadDebugData(data_temp, 2);
        if (ret == ESP_OK) {
            press_level = ((uint16_t)data_temp[1] << 8) | data_temp[0];
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

esp_err_t CsPressDriver::WritePressLevel(uint16_t press_level) {
    uint8_t data_temp[4] = {0};
    uint16_t read_press_level = 0;

    WakeupI2c();

    CleanDebugMode();
    SetDebugMode(AP_W_PRESS_LEVEL_DEBUG_MODE);

    data_temp[0] = 0;
    data_temp[1] = press_level & 0xFF;
    data_temp[2] = (press_level >> 8) & 0xFF;
    data_temp[3] = data_temp[0] + data_temp[1] + data_temp[2];

    WriteDebugData(data_temp, 4);
    SetDebugReady(4);

    DelayMs(DEBUG_MODE_DELAY_TIME);

    ReadPressLevel(read_press_level);

    if (read_press_level != press_level) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t CsPressDriver::ReadPga(uint8_t& pga) {
    uint8_t read_temp[4] = {0};

    esp_err_t ret = I2cRead(AP_FORCEDATA_REG, read_temp, 1);

    if (ret == ESP_OK) {
        if ((read_temp[0] > 0x00) && (read_temp[0] < 0x06)) {
            pga = read_temp[0];
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

esp_err_t CsPressDriver::WritePga(uint8_t pga) {
    uint8_t retry = RETRY_NUM;
    esp_err_t ret = ESP_OK;

    do {
        if (ret != ESP_OK) {
            DelayMs(1);
        }
        ret = I2cWrite(AP_FORCEDATA_REG, &pga, 1);
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

esp_err_t CsPressDriver::SetKeyioOutEnable() {
    uint8_t retry = RETRY_NUM;
    uint8_t temp_data = 0;
    esp_err_t ret = ESP_OK;

    do {
        if (ret != ESP_OK) {
            DelayMs(1);
        }
        ret = I2cWrite(AP_KEYIO_OUT_REG, &temp_data, 1);
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

esp_err_t CsPressDriver::SetKeyioOutDisable() {
    uint8_t retry = RETRY_NUM;
    uint8_t temp_data = 1;
    esp_err_t ret = ESP_OK;

    do {
        if (ret != ESP_OK) {
            DelayMs(1);
        }
        ret = I2cWrite(AP_KEYIO_OUT_REG, &temp_data, 1);
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

esp_err_t CsPressDriver::ReadNoiseInit(uint16_t period_num) {
    uint8_t data_temp[2];
    esp_err_t ret = ESP_OK;

    WakeupI2c();

    ret |= CleanDebugMode();
    ret |= SetDebugMode(AP_R_NOISE_DEBUG_MODE);

    data_temp[0] = period_num & 0xFF;
    data_temp[1] = (period_num >> 8) & 0xFF;
    ret |= WriteDebugData(data_temp, 2);

    ret |= SetDebugReady(2);

    return ret;
}

esp_err_t CsPressDriver::ReadNoise(int16_t& noise_data) {
    uint8_t num;
    uint8_t data_temp[3] = {0};
    uint8_t checksum;

    WakeupI2c();

    num = GetDebugReady();

    if (num == 3) {
        ReadDebugData(data_temp, 3);

        checksum = data_temp[0] + data_temp[1];

        if (checksum == data_temp[2]) {
            noise_data = (int16_t)(((uint16_t)data_temp[1] << 8) | data_temp[0]);
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

esp_err_t CsPressDriver::FwForceUpdate(const uint8_t* fw_array) {
    uint32_t fw_code_length;
    uint32_t fw_block_num;
    const uint8_t* fw_code_start;
    uint32_t fw_count;
    uint8_t fw_read_code[FW_ONE_BLOCK_LENGTH];
    uint16_t fw_default_version;
    uint16_t fw_read_version;

    // fw init
    fw_code_length = ((uint16_t)fw_array[FW_ADDR_CODE_LENGTH + 0] << 8) | fw_array[FW_ADDR_CODE_LENGTH + 1];
    fw_code_start = &fw_array[32];
    fw_block_num = fw_code_length / FW_ONE_BLOCK_LENGTH;
    fw_default_version = ((uint16_t)fw_array[FW_ADDR_VERSION + 0] << 8) | fw_array[FW_ADDR_VERSION + 1];

    ESP_LOGI(TAG, "FW update: code_length=%lu, block_num=%lu, version=0x%04X",
             fw_code_length, fw_block_num, fw_default_version);

#if SOFT_RESET_ENABLE
    SoftReset();

    int retry = 5;  
    int status;
    do {
        DelayMs(50);
        status = CheckFwStatus();
    } while ((status != 0) && (retry--));

    if (status != 0) {
        ESP_LOGE(TAG, "Cannot enter boot mode");
        return ESP_FAIL;
    }
#else
    PowerDown();
    DelayMs(100);
    PowerUp();
    DelayMs(60);
#endif

    // send fw write cmd
    I2cWrite(BOOT_CMD_REG, boot_fw_write_cmd, BOOT_CMD_LENGTH);
    DelayMs(10);

    // send fw code
    fw_count = 0;
    for (uint32_t i = 0; i < fw_block_num; i++) {
        esp_err_t ret = I2cWrite(i, fw_code_start + fw_count, FW_ONE_BLOCK_LENGTH);

        fw_count += FW_ONE_BLOCK_LENGTH;

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FW write failed at block %lu", i);
            return ESP_FAIL;
        }

        DelayMs(30);
    }

    // send fw read cmd
    I2cWrite(BOOT_CMD_REG, boot_fw_read_cmd, BOOT_CMD_LENGTH);
    DelayMs(10);

    // read & check fw code
    fw_count = 0;
    for (uint32_t i = 0; i < fw_block_num; i++) {
        esp_err_t ret = I2cRead(i, fw_read_code, FW_ONE_BLOCK_LENGTH);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FW read failed at block %lu", i);
            return ESP_FAIL;
        }

        // check code data
        for (int j = 0; j < FW_ONE_BLOCK_LENGTH; j++) {
            if (fw_read_code[j] != fw_code_start[fw_count + j]) {
                ESP_LOGE(TAG, "FW verify failed at block %lu, byte %d", i, j);
                return ESP_FAIL;
            }
        }

        fw_count += FW_ONE_BLOCK_LENGTH;
        DelayMs(15);
    }

    // send fw flag cmd
    I2cWrite(BOOT_CMD_REG, boot_fw_wflag_cmd, BOOT_CMD_LENGTH);
    DelayMs(50);

    // check fw version
    DelayMs(400);  // skip boot

    status = CheckFwStatus();

    if (status != 0) {  // not in boot status
        I2cRead(AP_VERSION_REG, fw_read_code, CS_FW_VERSION_LENGTH);

        fw_read_version = ((uint16_t)fw_read_code[1] << 8) | fw_read_code[0];

        if (fw_read_version != fw_default_version) {
            ESP_LOGE(TAG, "FW version mismatch: read=0x%04X, expected=0x%04X",
                     fw_read_version, fw_default_version);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "FW update success, version=0x%04X", fw_read_version);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "FW update failed, still in boot mode");
    return ESP_FAIL;
}

esp_err_t CsPressDriver::FwHighVersionUpdate(const uint8_t* fw_array) {
    uint8_t retry = RETRY_NUM;
    esp_err_t ret;

    DelayMs(400);  // skip boot jump time

    int status = CheckFwStatus();
    (void)status;  // 暂时忽略状态检查

    // 直接更新固件
    do {
        ret = FwForceUpdate(fw_array);
    } while ((ret != ESP_OK) && (retry--));

    return ret;
}

const uint8_t* CsPressDriver::GetDefaultFwArray() {
    return cs_press_default_fw_array;
}

size_t CsPressDriver::GetDefaultFwSize() {
    return cs_press_get_fw_size();
}

#endif // CONFIG_BOARD_TYPE_ESP32S3_BLEHD_V1

