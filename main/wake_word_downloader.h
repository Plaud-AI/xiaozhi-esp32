#ifndef WAKE_WORD_DOWNLOADER_H
#define WAKE_WORD_DOWNLOADER_H

#include <atomic>
#include <string>
#include <functional>

/**
 * 唤醒词模型下载器
 *
 * 通过 HTTP 将 .tflite 模型文件下载到 SPIFFS model 分区，
 * 下载完成后校验 MD5 完整性。全程在独立 FreeRTOS Task 中异步执行。
 *
 * 下载路径格式: /spiffs/{wakeword_id}.tflite
 * 参考 main/ota.cc 的 HTTP 下载实现。
 */
class WakeWordDownloader {
public:
    using ProgressCallback = std::function<void(int progress, int downloaded, int total)>;
    using CompleteCallback = std::function<void(bool success, const std::string& error)>;

    static WakeWordDownloader& GetInstance();

    WakeWordDownloader(const WakeWordDownloader&) = delete;
    WakeWordDownloader& operator=(const WakeWordDownloader&) = delete;

    /**
     * 异步下载唤醒词模型到 SPIFFS
     *
     * @param wakeword_id  训练服务分配的模型 ID（用于生成文件名）
     * @param url          .tflite 文件 CDN 直链
     * @param expected_md5 下载后用于校验完整性的 MD5 字符串
     * @param expected_size 预期文件字节数（用于进度计算）
     * @param wake_word_text 唤醒词文字（透传给回调，供 WakeWordManager 使用）
     * @param display_name 显示名称（透传给回调）
     * @param on_progress  进度回调 (progress 0-100, downloaded_bytes, total_bytes)
     * @param on_complete  完成回调 (success, error_message)
     */
    void StartDownload(
        const std::string& wakeword_id,
        const std::string& url,
        const std::string& expected_md5,
        int expected_size,
        const std::string& wake_word_text,
        const std::string& display_name,
        ProgressCallback on_progress,
        CompleteCallback on_complete
    );

    /**
     * 取消当前下载（若有）
     */
    void Cancel();

    /**
     * 获取当前下载的 wakeword_id（若无下载则返回空字符串）
     */
    std::string GetCurrentWakewordId() const;

    /**
     * 计算指定文件的 MD5（十六进制小写 32 字符）。失败返回空串。
     * 暴露给 WakeWordManager::LoadOnBoot 做启动时完整性校验。
     */
    static std::string ComputeFileMd5(const std::string& spiffs_path);

private:
    WakeWordDownloader() = default;
    ~WakeWordDownloader() = default;

    struct DownloadTask {
        std::string wakeword_id;
        std::string url;
        std::string expected_md5;
        int expected_size;
        std::string wake_word_text;
        std::string display_name;
        ProgressCallback on_progress;
        CompleteCallback on_complete;
    };

    static void DownloadTaskFunc(void* arg);

    std::atomic<bool> is_downloading_{false};
    std::atomic<bool> cancel_requested_{false};
    std::string current_wakeword_id_;

    static constexpr const char* TAG = "WakeWordDownloader";
    static constexpr const char* SPIFFS_BASE_PATH = "/spiffs";
};

#endif // WAKE_WORD_DOWNLOADER_H
