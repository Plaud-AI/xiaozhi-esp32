#pragma once

#include <vector>
#include <map>
#include <cstdint>
#include <string>
#include <mutex>

namespace micro_wake_word {

/**
 * @brief 推理测试数据记录器
 * 
 * 记录一次完整唤醒词检测过程中的：
 * 1. 原始 PCM 音频数据（从 Start 到 Detected）
 * 2. 每次模型推理输出的原始概率（滑动窗口平均前的 uint8 值）
 * 
 * 用于设备端与服务器端推理结果对比测试
 */
class InferenceTestRecorder {
public:
    /**
     * @brief 上传数据包
     * 包含一次完整检测过程的所有数据
     */
    struct UploadPacket {
        std::vector<int16_t> pcm_data;      // PCM 音频数据
        std::map<std::string, std::vector<uint8_t>> model_probabilities;  // 每个模型的概率序列 (model_name -> probabilities)
        std::string wake_word;               // 检测到的唤醒词
        float final_probability;             // 最终检测概率（滑动窗口平均）
        uint32_t duration_ms;                // 检测时长
    };

    InferenceTestRecorder();
    ~InferenceTestRecorder();

    //========== 检测生命周期 ==========//
    
    /**
     * @brief 标记检测开始
     * 在 MicroWakeWord::Start() 时调用
     * 清空之前的数据，开始新的记录
     */
    void OnDetectionStart();
    
    /**
     * @brief 标记检测结束，返回打包好的数据用于上传
     * 在检测到唤醒词时调用
     * @param wake_word 检测到的唤醒词名称
     * @param probability 最终检测概率
     * @return 打包好的数据，可直接提交上传
     */
    UploadPacket OnDetectionEnd(const std::string& wake_word, float probability);
    
    /**
     * @brief 标记检测取消
     * 在 MicroWakeWord::Stop() 时调用（未检测到唤醒词）
     * 清空数据，不上传
     */
    void OnDetectionCancelled();

    //========== 数据记录 ==========//
    
    /**
     * @brief 记录 PCM 数据
     * 在 MicroWakeWord::Feed() 中调用，记录输入的原始音频
     * @param data PCM 数据指针 (int16_t)
     * @param samples 样本数
     */
    void RecordPCM(const int16_t* data, size_t samples);
    
    /**
     * @brief 记录单次推理概率
     * 在 StreamingModel::perform_streaming_inference() 中调用
     * @param model_name 模型名称（用于区分不同模型的概率数据）
     * @param raw_probability 模型原始输出 (0-255, 滑动窗口平均前)
     */
    void RecordProbability(const std::string& model_name, uint8_t raw_probability);

    //========== 状态查询 ==========//
    
    /** 是否正在记录 */
    bool IsRecording() const { return recording_; }
    
    /** 获取已记录的 PCM 样本数 */
    size_t GetPCMSampleCount() const;
    
    /** 获取已记录的概率数量 */
    size_t GetProbabilityCount() const;

private:
    void Reset();

    bool recording_ = false;
    uint32_t start_time_ms_ = 0;
    
    // PCM 数据缓冲
    std::vector<int16_t> pcm_data_;
    
    // 每个模型的概率数据缓冲
    std::map<std::string, std::vector<uint8_t>> model_probabilities_;
    
    mutable std::mutex mutex_;
    
    // 配置常量 - 使用环形缓冲，只保留最近 N 秒的数据
    static constexpr uint32_t kSampleRate = 16000;
    static constexpr uint32_t kMaxDurationMs = 21000;  // 保留最近 21 秒音频（覆盖 20s 超时 + 余量）
    static constexpr size_t kMaxPCMSamples = kSampleRate * kMaxDurationMs / 1000;  // 336000 samples = 656KB
    static constexpr size_t kMaxProbabilities = 2100;  // 保留最近 2100 次推理结果（覆盖 21 秒）
};

}  // namespace micro_wake_word

