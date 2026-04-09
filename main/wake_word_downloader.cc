#include "wake_word_downloader.h"
#include "board.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

// MD5 – use mbedtls which is always available on ESP-IDF
#include <mbedtls/md5.h>

WakeWordDownloader& WakeWordDownloader::GetInstance() {
    static WakeWordDownloader instance;
    return instance;
}

// ──────────────────────────────────────────────────────────
// 内部：挂载 "model" SPIFFS 分区（幂等，多次调用无副作用）
// ──────────────────────────────────────────────────────────
static bool EnsureModelSpiffsMounted() {
    // 已挂载则直接返回
    if (esp_spiffs_mounted("model")) {
        return true;
    }
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/model",
        .partition_label = "model",
        .max_files = 4,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE("WakeWordDownloader", "挂载 model SPIFFS 失败: %s", esp_err_to_name(ret));
        return false;
    }
    return true;
}

// ──────────────────────────────────────────────────────────
// 内部：计算已下载文件的 MD5（十六进制字符串）
// ──────────────────────────────────────────────────────────
std::string WakeWordDownloader::ComputeFileMd5(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";

    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);

    uint8_t buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        mbedtls_md5_update(&ctx, buf, n);
    }
    fclose(f);

    uint8_t digest[16];
    mbedtls_md5_finish(&ctx, digest);
    mbedtls_md5_free(&ctx);

    char hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    return std::string(hex, 32);
}

// ──────────────────────────────────────────────────────────
// FreeRTOS Task 入口（arg = new DownloadTask，需自行 delete）
// ──────────────────────────────────────────────────────────
void WakeWordDownloader::DownloadTaskFunc(void* arg) {
    auto* task = static_cast<DownloadTask*>(arg);
    auto& self = WakeWordDownloader::GetInstance();

    ESP_LOGI(TAG, "开始下载唤醒词模型: %s", task->wakeword_id.c_str());
    ESP_LOGI(TAG, "URL: %s", task->url.c_str());

    // 1. 挂载 SPIFFS
    if (!EnsureModelSpiffsMounted()) {
        task->on_complete(false, "挂载 model SPIFFS 失败");
        delete task;
        self.is_downloading_ = false;
        vTaskDelete(nullptr);
        return;
    }

    // 2. 打开 HTTP 连接
    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(0);
    http->SetTimeout(30000);

    if (!http->Open("GET", task->url)) {
        task->on_complete(false, "HTTP 连接失败");
        delete task;
        self.is_downloading_ = false;
        vTaskDelete(nullptr);
        return;
    }

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "HTTP 状态码错误: %d", http->GetStatusCode());
        http->Close();
        task->on_complete(false, "HTTP 状态码非 200");
        delete task;
        self.is_downloading_ = false;
        vTaskDelete(nullptr);
        return;
    }

    size_t content_length = http->GetBodyLength();
    if (content_length == 0 && task->expected_size > 0) {
        content_length = task->expected_size;
    }

    // 3. 打开目标文件
    std::string spiffs_path = std::string("/model/") + task->wakeword_id + ".tflite";
    FILE* f = fopen(spiffs_path.c_str(), "wb");
    if (!f) {
        ESP_LOGE(TAG, "无法创建文件: %s", spiffs_path.c_str());
        http->Close();
        task->on_complete(false, "无法创建文件");
        delete task;
        self.is_downloading_ = false;
        vTaskDelete(nullptr);
        return;
    }

    // 4. 循环读取并写入文件
    char buffer[1024];
    size_t total_read = 0;
    auto last_report_time = esp_timer_get_time();

    while (!self.cancel_requested_) {
        int n = http->Read(buffer, sizeof(buffer));
        if (n < 0) {
            fclose(f);
            http->Close();
            remove(spiffs_path.c_str());
            task->on_complete(false, "HTTP 读取失败");
            delete task;
            self.is_downloading_ = false;
            vTaskDelete(nullptr);
            return;
        }
        if (n == 0) break; // 下载完成

        if (fwrite(buffer, 1, n, f) != (size_t)n) {
            fclose(f);
            http->Close();
            remove(spiffs_path.c_str());
            task->on_complete(false, "文件写入失败（SPIFFS 空间不足？）");
            delete task;
            self.is_downloading_ = false;
            vTaskDelete(nullptr);
            return;
        }

        total_read += n;

        // 每 500ms 上报一次进度
        if (esp_timer_get_time() - last_report_time >= 500000) {
            int progress = (content_length > 0)
                ? (int)(total_read * 100 / content_length)
                : 0;
            task->on_progress(progress, (int)total_read, (int)content_length);
            last_report_time = esp_timer_get_time();
        }
    }
    fclose(f);
    http->Close();

    if (self.cancel_requested_) {
        remove(spiffs_path.c_str());
        task->on_complete(false, "下载已取消");
        delete task;
        self.is_downloading_ = false;
        self.cancel_requested_ = false;
        vTaskDelete(nullptr);
        return;
    }

    // 上报 100%
    task->on_progress(100, (int)total_read, (int)total_read);

    // 5. MD5 校验
    if (!task->expected_md5.empty()) {
        std::string actual_md5 = ComputeFileMd5(spiffs_path);
        ESP_LOGI(TAG, "MD5 期望: %s", task->expected_md5.c_str());
        ESP_LOGI(TAG, "MD5 实际: %s", actual_md5.c_str());
        if (actual_md5 != task->expected_md5) {
            ESP_LOGE(TAG, "MD5 校验失败，删除文件");
            remove(spiffs_path.c_str());
            task->on_complete(false, "MD5 校验失败");
            delete task;
            self.is_downloading_ = false;
            vTaskDelete(nullptr);
            return;
        }
    }

    ESP_LOGI(TAG, "✅ 模型下载成功: %s (%d bytes)", spiffs_path.c_str(), (int)total_read);
    task->on_complete(true, "");
    delete task;
    self.is_downloading_ = false;
    vTaskDelete(nullptr);
}

// ──────────────────────────────────────────────────────────
// 公共接口
// ──────────────────────────────────────────────────────────
void WakeWordDownloader::StartDownload(
    const std::string& wakeword_id,
    const std::string& url,
    const std::string& expected_md5,
    int expected_size,
    const std::string& wake_word_text,
    const std::string& display_name,
    ProgressCallback on_progress,
    CompleteCallback on_complete)
{
    if (is_downloading_) {
        ESP_LOGW(TAG, "已有下载任务进行中，拒绝新任务");
        on_complete(false, "已有下载任务进行中");
        return;
    }

    is_downloading_ = true;
    cancel_requested_ = false;
    current_wakeword_id_ = wakeword_id;

    auto* task = new DownloadTask{
        wakeword_id,
        url,
        expected_md5,
        expected_size,
        wake_word_text,
        display_name,
        std::move(on_progress),
        std::move(on_complete),
    };

    xTaskCreate(DownloadTaskFunc, "ww_download", 8192, task, 5, nullptr);
}

void WakeWordDownloader::Cancel() {
    if (is_downloading_) {
        cancel_requested_ = true;
        ESP_LOGI(TAG, "下载取消请求已设置");
    }
}

std::string WakeWordDownloader::GetCurrentWakewordId() const {
    return is_downloading_ ? current_wakeword_id_ : "";
}
