#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "wake_word_manager.h"
#include "audio/wake_words/custom_wake_word.h"

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
#include "doll/doll_interaction_manager.h"
#include "doll/doll_mcp_tools.h"
#include "motor/motor_controller.h"
#endif

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    ESP_LOGI(TAG, "CheckAssetsVersion() called");
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        ESP_LOGI(TAG, "Will use built-in models from firmware");
        // 使用内置模型
        srmodel_list_t* models_list = esp_srmodel_init("model");
        if (models_list != nullptr) {
            ESP_LOGI(TAG, "Built-in models loaded, calling SetModelsList");
            audio_service_.SetModelsList(models_list);
        } else {
            ESP_LOGW(TAG, "Failed to load built-in models, but calling SetModelsList(nullptr) anyway");
            ESP_LOGI(TAG, "MicroWakeWord and other wake word implementations don't require MultiNet models");
            audio_service_.SetModelsList(nullptr);
        }
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // Apply assets
    ESP_LOGI(TAG, "Applying assets...");
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation (don't break, just fall through)
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
            ESP_LOGI(TAG, "║   📱 设备需要激活                     ║");
            ESP_LOGI(TAG, "╠════════════════════════════════════════╣");
            ESP_LOGI(TAG, "║   激活码: %s                ║", ota.GetActivationCode().c_str());
            ESP_LOGI(TAG, "║   消息: %s", ota.GetActivationMessage().c_str());
            ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Start the main event loop task with priority 3
    // Stack size: 14KB (balanced for OPUS resampler + WebSocket operations)
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 7, this, 3, &main_event_loop_task_handle_);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version or get the MQTT broker address
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    // Register doll interaction MCP tools
    RegisterDollMcpTools();
    ESP_LOGI(TAG, "Doll interaction MCP tools registered");
#endif

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        ESP_LOGD(TAG, "🎵 OnIncomingAudio: packet size=%zu, device_state=%d (%s)", 
                 packet->payload.size(), device_state_, STATE_STRINGS[device_state_]);
        if (device_state_ == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        } else {
            ESP_LOGW(TAG, "⚠️  Received audio but not in SPEAKING state, dropping packet");
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            ESP_LOGI(TAG, "📢 TTS event: state=%s", state->valuestring);
            if (strcmp(state->valuestring, "start") == 0) {
                ESP_LOGI(TAG, "🎙️  TTS started, switching to SPEAKING state");
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                ESP_LOGI(TAG, "🎙️  TTS stopped");
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else if (strcmp(command->valuestring, "start_config") == 0) {
                    ESP_LOGI(TAG, "Received start_config command from LLM");
                    Schedule([this]() {
                        StartConfigMode();
                    });
                } else if (strcmp(command->valuestring, "exit_config") == 0) {
                    ESP_LOGI(TAG, "Received exit_config command from LLM");
                    Schedule([this]() {
                        StopConfigMode();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    ESP_LOGI(TAG, "📊 Pre-initialization memory check:");
    SystemInfo::PrintHeapStats();
    
#ifdef CONFIG_ENABLE_DOLL_INTERACTION
    // CRITICAL: Initialize Doll system BEFORE SetDeviceState(IDLE)
    // Because IDLE state will try to Start() it if not running
    ESP_LOGI(TAG, "🎭 Initializing doll interaction system...");
    
    // Initialize motor controller first
    MotorController::GetInstance().Initialize();
    MotorController::GetInstance().Start();
    
    // Initialize doll interaction manager (but don't Start yet)
    DollInteractionManager::GetInstance().Initialize();
    
    ESP_LOGI(TAG, "✅ Doll interaction system initialized");
#endif
    
    // Set IDLE state to initialize MicroWakeWord while SRAM is still available
    ESP_LOGI(TAG, "⚠️  Initializing MicroWakeWord (requires ~320 bytes SRAM)...");
    SetDeviceState(kDeviceStateIdle);  // This will Start() Doll system if initialized
    
    // Wait for initialization to complete
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_LOGI(TAG, "📊 Post-initialization memory:");
    SystemInfo::PrintHeapStats();

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::OnWakeWordDetected() {  
    ESP_LOGI(TAG, "OnWakeWordDetected() called, device_state=%d, protocol=%p", 
             device_state_, protocol_.get());
    
    if (!protocol_) {
        ESP_LOGW(TAG, "Protocol not initialized, ignoring wake word");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        ESP_LOGI(TAG, "Device in IDLE state, processing wake word...");
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            ESP_LOGI(TAG, "Opening audio channel...");
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                ESP_LOGE(TAG, "Failed to open audio channel, re-enabling wake word detection");
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "*** Wake word detected: %s ***", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // Encode and send the wake word data to the server
        ESP_LOGI(TAG, "📤 Sending wake word packets...");
        int packet_count = 0;
        // Throttle the sending speed to avoid starving WiFi driver buffers
        const int THROTTLE_DELAY_MS = 20; 
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            packet_count++;
            ESP_LOGD(TAG, "  Sending packet #%d, size=%zu", packet_count, packet->payload.size());
            if (!protocol_->SendAudio(std::move(packet))) {
                ESP_LOGE(TAG, "❌ Failed to send wake word packet #%d", packet_count);
                break;
            }
            // Wait a bit to let WiFi driver process the packet
            vTaskDelay(pdMS_TO_TICKS(THROTTLE_DELAY_MS));
        }
        ESP_LOGI(TAG, "✅ Sent %d wake word packets", packet_count);
        
        // Set the chat state to wake word detected
        ESP_LOGI(TAG, "📡 Sending wake word detected event: '%s'", wake_word.c_str());
        protocol_->SendWakeWordDetected(wake_word);
        
        ESP_LOGI(TAG, "🎤 Setting listening mode...");
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        ESP_LOGI(TAG, "✅ Wake word processing complete");
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

#include "ble_wifi_provisioner.h"

// ... existing includes ...

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    // ⚠️ CRITICAL: Ensure BLE Provisioner is STOPPED when entering active states
    // to prevent BLE controller from crashing due to coexistence issues with WiFi/Audio.
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        BLEWiFiProvisioner::GetInstance().Stop();
    }

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            ESP_LOGI(TAG, "Entering IDLE state, enabling wake word detection...");
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            
#ifdef CONFIG_ENABLE_DOLL_INTERACTION
            // Restore Doll system after AFE is stopped
            if (!DollInteractionManager::GetInstance().IsRunning()) {
                ESP_LOGI(TAG, "🎭 Restoring Doll system (AFE stopped, SRAM available)...");
                // ⚠️ IMPORTANT: Must call Start() only, Initialize() was already called at startup
                // The DollInteractionManager is designed to be initialized once and can be started/stopped multiple times
                DollInteractionManager::GetInstance().Start();
                ESP_LOGI(TAG, "✅ Doll system restored");
            }
#endif
            
            ESP_LOGI(TAG, "IDLE state setup complete");
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("connecting");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("listening");

            // Make sure the audio processor is running
            if (!audio_service_.IsAudioProcessorRunning()) {
                // CRITICAL: Free SRAM before creating AFE task
                
#ifdef CONFIG_ENABLE_DOLL_INTERACTION
                // Step 1: Stop Doll system to free ~4KB SRAM
                if (DollInteractionManager::GetInstance().IsRunning()) {
                    ESP_LOGI(TAG, "🎭 Stopping Doll system to free SRAM for AFE...");
                    DollInteractionManager::GetInstance().Stop();
                    ESP_LOGI(TAG, "📊 After stopping Doll: free SRAM=%zu", 
                             heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                }
#endif
                
                // Step 2: Disable wake word to free MicroWakeWord memory
                audio_service_.EnableWakeWordDetection(false);
                
                // Step 3: Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                
                // Step 4: Enable AFE (should now have enough SRAM)
                audio_service_.EnableVoiceProcessing(true);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);
            display->SetEmotion("speaking");

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    // Use provided URL or get from OTA object
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    
    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveMode(true); // Restore power save mode
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // Make sure you are using main thread to send MCP message
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    } else {
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

void Application::PlaySuccessSound() {
    ESP_LOGI(TAG, "🔊 播放成功提示音");
    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
}

#include "ble_wifi_provisioner.h"

void Application::StartConfigMode() {
    if (device_state_ == kDeviceStateWifiConfiguring) {
        ESP_LOGW(TAG, "Already in config mode, skipping");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🚀 进入配置模式 (Config Mode)");
    ESP_LOGI(TAG, "   - 停止音频服务");
    ESP_LOGI(TAG, "   - 关闭音频连接");
    ESP_LOGI(TAG, "   - 启动 BLE 广播");
    ESP_LOGI(TAG, "========================================");

    // 1. 切换状态，防止其他逻辑干扰
    SetDeviceState(kDeviceStateWifiConfiguring);

    // 2. 停止音频服务 (关键！释放 CPU 和 避免 WiFi 数据流)
    // 暂时停止唤醒词检测和语音处理
    audio_service_.EnableWakeWordDetection(false);
    audio_service_.EnableVoiceProcessing(false);
    
    // 3. 关闭当前可能存在的音频连接
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel...");
        protocol_->CloseAudioChannel();
    }

    // 4. 更改显示，提示用户 (静态画面，降低渲染负载)
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(Lang::Strings::CONFIGURING);
    // 确保有一个低负载的动画或静态图，这里暂时用 neutral
    // 理想情况下应该有一个 "bluetooth" 或 "settings" 的 lottie
    display->SetEmotion("neutral"); 
    display->SetChatMessage("system", "蓝牙已开启\n请通过手机连接配置");

    // 5. 启动 BLE
    // 注意：BLEWiFiProvisioner::Start 内部已经有禁用 WiFi PS 的逻辑
    ESP_LOGI(TAG, "Starting BLE Provisioner...");
    BLEWiFiProvisioner::GetInstance().Start();
}

void Application::StopConfigMode() {
    if (device_state_ != kDeviceStateWifiConfiguring) {
        ESP_LOGW(TAG, "Not in config mode, skipping exit");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "🛑 退出配置模式");
    ESP_LOGI(TAG, "   - 停止 BLE 广播");
    ESP_LOGI(TAG, "   - 恢复 IDLE 状态");
    ESP_LOGI(TAG, "========================================");

    // 1. 停止 BLE
    BLEWiFiProvisioner::GetInstance().Stop();

    // 2. 恢复状态到 Idle
    // SetDeviceState(Idle) 会自动恢复唤醒词检测和默认表情
    // 见 Application::SetDeviceState 中的 switch case
    SetDeviceState(kDeviceStateIdle);
    
    auto display = Board::GetInstance().GetDisplay();
    display->SetChatMessage("system", "");
}

bool Application::ApplyWakeWordConfig() {
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  🔄 Application::ApplyWakeWordConfig                     ║");
    ESP_LOGI(TAG, "║     运行时应用唤醒词配置（无需重启）                      ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
    
    // 获取 CustomWakeWord 指针
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "步骤 1: 获取 CustomWakeWord 对象...");
    auto* wake_word = audio_service_.GetWakeWord();
    if (!wake_word) {
        ESP_LOGE(TAG, "❌ Wake word object is NULL");
        ESP_LOGE(TAG, "   可能原因: AudioService 未正确初始化");
        return false;
    }
    ESP_LOGI(TAG, "✅ 成功获取 WakeWord 对象");
    
    // 尝试转换为 CustomWakeWord
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "步骤 2: 检查 WakeWord 类型...");
    CustomWakeWord* custom_wake_word = dynamic_cast<CustomWakeWord*>(wake_word);
    if (!custom_wake_word) {
        ESP_LOGE(TAG, "❌ Wake word is not CustomWakeWord type");
        ESP_LOGE(TAG, "   运行时更新仅支持 CustomWakeWord");
        ESP_LOGE(TAG, "   当前类型可能是其他唤醒词实现");
        return false;
    }
    ESP_LOGI(TAG, "✅ WakeWord 类型正确 (CustomWakeWord)");
    
    // 加载配置并应用
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "步骤 3: 从 NVS 加载唤醒词配置...");
    auto& manager = WakeWordManager::GetInstance();
    if (!manager.LoadFromNVS()) {
        ESP_LOGW(TAG, "⚠️  NVS 中没有保存的唤醒词配置");
        ESP_LOGW(TAG, "   可能是首次启动或配置被清除");
        return false;
    }
    
    int wake_word_count = manager.GetCount();
    float threshold = manager.GetThreshold();
    ESP_LOGI(TAG, "✅ 成功从 NVS 加载配置:");
    ESP_LOGI(TAG, "   唤醒词数量: %d", wake_word_count);
    ESP_LOGI(TAG, "   阈值: %.3f", threshold);
    
    // 应用到 CustomWakeWord
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "步骤 4: 应用配置到 CustomWakeWord...");
    bool success = manager.ApplyToCustomWakeWord(custom_wake_word);
    
    if (success) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════╗");
        ESP_LOGI(TAG, "║  ✅✅✅ 唤醒词配置应用成功！                              ║");
        ESP_LOGI(TAG, "║  📢 %d 个唤醒词已立即生效，无需重启设备                 ║", wake_word_count);
        ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════╝");
        return true;
    } else {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔══════════════════════════════════════════════════════════╗");
        ESP_LOGE(TAG, "║  ❌ 唤醒词配置应用失败                                    ║");
        ESP_LOGE(TAG, "║  建议: 重启设备后唤醒词将自动加载                         ║");
        ESP_LOGE(TAG, "╚══════════════════════════════════════════════════════════╝");
        return false;
    }
}