#pragma once

#include "inference_test_recorder.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string>
#include <atomic>

namespace micro_wake_word {

/**
 * @brief 推理测试数据上传器
 * 
 * 独立任务，从队列接收数据并通过 HTTP POST 上传到服务器
 * 
 * 上传接口：
 * - PCM 数据: POST {server_url}/upload/bytes (raw binary)
 * - 概率数据: POST {server_url}/upload/text (逗号分隔的浮点数)
 */
class InferenceTestUploader {
public:
    InferenceTestUploader();
    ~InferenceTestUploader();

    /**
     * @brief 启动上传任务
     * @return 成功返回 true
     */
    bool Start();
    
    /**
     * @brief 停止上传任务
     */
    void Stop();
    
    /**
     * @brief 提交数据到上传队列（非阻塞）
     * @param packet 要上传的数据包（会被移动）
     * @return 成功加入队列返回 true，队列满返回 false
     */
    bool Submit(InferenceTestRecorder::UploadPacket&& packet);

    /**
     * @brief 配置服务器地址
     * @param url 服务器 URL，如 "http://192.168.1.100:7007"
     */
    void SetServerUrl(const std::string& url) { server_url_ = url; }
    
    /**
     * @brief 获取服务器地址
     */
    const std::string& GetServerUrl() const { return server_url_; }
    
    /**
     * @brief 检查上传器是否运行中
     */
    bool IsRunning() const { return running_; }

private:
    static void UploadTaskEntry(void* param);
    void UploadTask();
    
    /**
     * @brief 上传 PCM 数据
     * POST {server_url}/upload/bytes
     * @return 成功返回 true
     */
    bool UploadPCM(const std::vector<int16_t>& pcm_data);
    
    /**
     * @brief 上传概率数据
     * POST {server_url}/upload/text
     * @return 成功返回 true
     */
    bool UploadProbabilities(const std::vector<uint8_t>& probabilities);
    
    /**
     * @brief 保存字节数据到服务器文件
     * POST {server_url}/save/bytes
     * @return 成功返回 true
     */
    bool SaveBytes();
    
    /**
     * @brief 保存文本数据到服务器文件
     * POST {server_url}/save/text
     * @return 成功返回 true
     */
    bool SaveText();

    // 服务器配置
    std::string server_url_ = "http://115.190.161.149:7007";
    
    // FreeRTOS 资源
    QueueHandle_t upload_queue_ = nullptr;
    TaskHandle_t upload_task_ = nullptr;
    std::atomic<bool> running_{false};
    
    // 配置常量
    static constexpr size_t kQueueSize = 3;           // 队列最多缓存 3 个数据包
    static constexpr size_t kTaskStackSize = 8192;    // 任务栈大小
    static constexpr UBaseType_t kTaskPriority = 5;   // 任务优先级（较低）
    static constexpr int kHttpTimeoutMs = 15000;      // HTTP 超时 15 秒
};

}  // namespace micro_wake_word

