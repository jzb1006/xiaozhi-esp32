#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"
#include "settings.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include <vector>

#define TAG "MuseLabNanoEsp32C6Pdm"

class MuseLabNanoEsp32C6PdmBoard : public WifiBoard {
private:
    Button boot_button_;

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeAutoStartListening() {
#if AUTO_START_LISTENING_AFTER_BOOT
        xTaskCreate([](void*) {
            auto& app = Application::GetInstance();
            for (int i = 0; i < 60; ++i) {
                if (app.GetDeviceState() == kDeviceStateIdle) {
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    app.StartListening();
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            vTaskDelete(nullptr);
        }, "auto_listen", 4096, nullptr, 1, nullptr);
#endif
    }

    void InitializeMicSelfTest() {
#if MIC_SELF_TEST_AFTER_BOOT
        xTaskCreate([](void* arg) {
            auto* board = static_cast<MuseLabNanoEsp32C6PdmBoard*>(arg);
            auto* codec = board->GetAudioCodec();
            std::vector<int16_t> data(512);
            vTaskDelay(pdMS_TO_TICKS(2000));
            codec->EnableOutput(true);
            codec->EnableInput(true);
            ESP_LOGI(TAG, "INMP441 I2S mic self-test started");
            while (true) {
                if (!codec->InputData(data)) {
                    ESP_LOGW(TAG, "INMP441 I2S mic self-test read timeout");
                    vTaskDelay(pdMS_TO_TICKS(200));
                }
            }
        }, "mic_self_test", 4096, this, 1, nullptr);
#endif
    }

    void InitializeDefaultVolume() {
        Settings settings("audio", false);
        if (settings.HasInt("output_volume")) {
            return;
        }

        Settings persist("audio", true);
        persist.SetInt("output_volume", AUDIO_DEFAULT_VOLUME);
    }

public:
    MuseLabNanoEsp32C6PdmBoard() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeButtons();
        InitializeDefaultVolume();
        InitializeAutoStartListening();
        InitializeMicSelfTest();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
            AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_DIN);
        static bool input_gain_initialized = false;
        if (!input_gain_initialized) {
            audio_codec.SetInputGain(1.0f);
            input_gain_initialized = true;
        }
        return &audio_codec;
    }
};

DECLARE_BOARD(MuseLabNanoEsp32C6PdmBoard);
