# MuseLab C6 限时续聊与播放期上行实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让 MuseLab nanoESP32-C6 在待机时只做本地唤醒词检测、不向服务器上传麦克风音频；被唤醒、限时续聊窗口和播放期打断窗口内才上传语音。

**架构：** 固件侧保留本地 wake word 常开作为待机入口，把“麦克风硬件工作”和“音频上行到服务器”解耦。`Idle` 状态只启用 wake word feed，不启用 voice processor；`Listening` 状态启用 voice processor 并发送 `listen.start`；`Speaking` 状态为方案 C 增加播放期上行模式，允许服务端判定用户打断。回答结束后进入 4.5 秒限时续聊窗口，窗口到期由固件发送 `listen.stop` 并回到 `Idle`；当前 C6 路径没有本地有效语音续期能力，后续轮次是否成立由服务端在窗口内返回下一轮 `tts.start` 决定。

**技术栈：** ESP-IDF 5.5.x、C++17、FreeRTOS event group、`esp_timer`、小智 WebSocket 协议、Opus 语音上行、Wakenet without AFE、MuseLab nanoESP32-C6 PDM。

---

## 关键结论

- 这不是单纯 C6 硬件限制。C6 没有 S3 AFE/AEC 能力，播放时本地抗回声能力弱，但“待机不上行”和“限时续聊窗口”主要是固件状态机和协议时序问题。
- 待机状态麦克风可以继续本地读音频给 wake word 模型，但不能启用 `AudioProcessor`，因此不会通过 WebSocket 上传给服务器。
- 回答后不能直接长期保持 `Listening`。应进入 4.5 秒左右的 follow-up 窗口，超时回 `Idle`。
- 用户选择了方案 C：播放期间固件继续上传麦克风音频，由服务端做 ASR 和打断判定。C6 无 AEC，因此服务端必须加误触发防护，固件侧只负责提供窗口和协议信号。
- 现有 C6 `tts.stop` 分支直接 `SetDeviceState(kDeviceStateIdle)`，会牺牲连续对话；本计划要把它改成“进入限时续聊窗口”，而不是永久监听。
- `AudioService::EnableVoiceProcessing(true)` 当前会无条件调用 `ResetDecoder()`，`ResetDecoder()` 会清空 `audio_decode_queue_` 和 `audio_playback_queue_`。因此播放期上行不能在 `Speaking` 中直接调用 `EnableVoiceProcessing(true)`；必须先给 `AudioService` 增加不清 decoder 的受控入口，再在 C6 `Speaking` 分支使用它。
- C6 的 4.5 秒 `auto_stop_listening_timer_handle_` 已存在于 `Application`，但它是固定窗口，不读取 VAD，也不会因用户正在说话自动续期。`NoAudioProcessor` 当前只是攒帧输出，没有有效 VAD 判断。

## 参考 C3 方案边界

C3 系列可参考的是“资源受限芯片使用 Wakenet without AFE，本地唤醒词常开，唤醒后才进入语音处理/上行”的思路：

- `/Users/jiangzhibin/workspace/xiaozhi-esp32/main/boards/xmini-c3/config.json` 和 `/Users/jiangzhibin/workspace/xiaozhi-esp32/main/boards/lichuang-c3-dev/config.json` 都启用 `CONFIG_USE_ESP_WAKE_WORD=y`，对应当前非 S3/P4 路径的 `EspWakeWord`。
- `xmini-c3`、`lichuang-c3-dev` 的 BOOT 按键都走 `Application::ToggleChatState()`，这一点可作为 C6 BOOT 手动切换兼容性参考。
- `esp-hi` 可参考的是 C3 + 轻量 codec/资源配置思路，不是对话状态机。它没有提供可照搬的播放期上行/AEC 实现。

不能照搬：

- C3/C6 都不是 S3 AFE 路线，`AudioService::IsAfeWakeWord()` 在非 S3/P4 返回 `false`，播放期本地 wake word/AEC 不能依赖 AFE。
- C3 参考板型没有解决“播放期不清 decoder 同时上行”的问题；C6 方案必须显式处理 `EnableVoiceProcessing(true)` 清播放队列的副作用。
- MuseLab C6 当前使用 `NoAudioCodecDuplex`、16 kHz 输入输出、`CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM`，不要把 C3 的 ES8311/ADC PDM 引脚或 24 kHz 配置照搬到 C6。

## 成功标准

- 开机待机后串口只出现 wake word 运行相关日志，不持续出现 `WS: Sent audio packets`。
- 唤醒词触发后发送 `listen.start mode=auto` 并开始上传音频；`listen.detect` 只在 `CONFIG_SEND_WAKE_WORD_DATA` 生效时出现，当前 C6 `CONFIG_USE_ESP_WAKE_WORD` 路径不把它作为硬性验收项。
- `tts.start` 后设备进入 `Speaking`，播放期间按方案 C 发送 `listen.start mode=barge_in` 或等价协议标记，并上传麦克风音频。
- 服务端下发 `tts.stop` 后，C6 进入 4.5 秒限时续聊窗口；窗口内固件持续上行，若服务端及时识别并下发下一轮 `tts.start` 则进入下一轮回答；窗口到期仍处于 `Listening` 则发送 `listen.stop` 并回 `Idle`。
- 回到 `Idle` 后，只保留本地 wake word，不继续向服务器发送二进制音频。
- BOOT 手动切换不破坏自动超时：用户手动停止时立即回 `Idle`，用户手动开始时进入 `Listening` 并复用超时保护。
- C6 的 BOOT 点击走 `Application::ToggleChatState()`，不是 `StartListening()`；因此 BOOT 从 `Idle` 手动开始应使用 `GetDefaultListeningMode()` 的 `AutoStop` 路径并启动 4.5 秒 timer。
- 构建命令通过：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
"/Users/jiangzhibin/.espressif/tools/ninja/1.12.1/ninja" -C "/Users/jiangzhibin/workspace/xiaozhi-esp32/build-muselab-nanoesp32-c6-pdm"
```

## 开发前环境前置

- 后续所有构建、刷机和串口监控命令都必须先加载 ESP-IDF 5.5.2 环境：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
```

- 不要在新 shell 中直接运行裸 `ninja -C ...`。当前仓库的 CMake 依赖 `IDF_PATH`，未加载环境时会把 `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` 解析成 `/tools/cmake/project.cmake`，导致 `idf_build_set_property` 未定义。
- 本计划只修固件状态机和协议时序；如果 ESP-IDF 环境本身缺失或损坏，应先中止实现并修复本机工具链，不在业务改动里绕过构建环境问题。

## 非目标

- 不在 C6 本地实现 AEC、声纹识别或复杂 VAD。
- 不修改非 MuseLab nanoESP32-C6 PDM 板型的默认对话行为。
- 不改变音频编解码格式和 OTA 协议主路径。
- 不在固件里做语义判断，例如“这是不是新问题”。
- 不做 git commit、push 或分支操作；实现后由用户明确确认再处理。

## 文件结构

- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.h`
  - 职责：声明 C6 follow-up/barge-in 状态辅助方法、计时器常量和必要状态字段；现有 `StartAutoStopListeningTimer()`、`StopAutoStopListeningTimer()` 已存在，按需新增播放期上行 helper。
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc`
  - 职责：调整 `tts.start`、`tts.stop`、`Idle`、`Listening`、`Speaking` 状态流转；实现限时续聊窗口；在播放期打断窗口启用上行。
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/protocols/protocol.h`
  - 职责：扩展 `ListeningMode` 或新增发送方法，表达 `barge_in` 模式。
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/protocols/protocol.cc`
  - 职责：把播放期上行标记编码为 `{"type":"listen","state":"start","mode":"barge_in"}`。
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio/audio_service.cc`
  - 职责：确认 `EnableWakeWordDetection(true)` 与 `EnableVoiceProcessing(false)` 时只本地 feed wake word，不进入 audio processor 上行路径；新增不清 decoder 的 C6 播放期 voice processor 启动入口；必要时增加日志。
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio/audio_service.h`
  - 职责：声明不清 decoder 的播放期 voice processor 启动入口。
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/boards/muselab-nanoesp32-c6-pdm/muselab_nanoesp32_c6_pdm_board.cc`
  - 职责：确认 BOOT 手动切换和自动续聊窗口兼容。

## 状态模型

```text
Idle
  mic: local wake word only
  uplink: off
  on wake word -> Listening(reason=wake_word)

Listening
  mic: voice processor
  uplink: on
  timer: fixed 4.5s for C6 auto/follow-up mode
  on listen.stop or timer -> Idle
  on server tts.start -> Speaking

Speaking
  speaker: on
  mic: voice processor for barge-in without resetting decoder
  uplink: on with mode=barge_in
  on BOOT manual abort -> Idle
  on server accepted barge-in / tts.stop -> Listening(follow-up)
  on tts.stop -> Listening(follow-up, 4.5s timer)
```

## 任务 1：固化 C6 待机不上行的状态基线

**文件：**
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc`
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio/audio_service.cc`

- [ ] **步骤 1：审查当前 Idle 行为**

读取：

```bash
sed -n '883,965p' "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc"
sed -n '230,285p' "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio/audio_service.cc"
sed -n '549,603p' "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio/audio_service.cc"
sed -n '668,690p' "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio/audio_service.cc"
```

确认基线：

```text
Idle:
  audio_service_.EnableVoiceProcessing(false)
  audio_service_.EnableWakeWordDetection(true)

AudioInputTask:
  AS_EVENT_WAKE_WORD_RUNNING -> wake_word_->Feed(data)
  AS_EVENT_AUDIO_PROCESSOR_RUNNING -> audio_processor_->Feed(data)
```

- [ ] **步骤 2：补充最小日志，区分本地唤醒和上行处理**

在 `HandleStateChangedEvent()` 的 C6 `Idle` 分支附近加入日志：

```cpp
#if CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM
            ESP_LOGI(TAG, "C6 idle: wake word local only, voice uplink disabled");
#endif
```

在 C6 `Listening` 分支启用 voice processor 前加入日志。日志应放在 `if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning())` 内，避免状态重入时误报：

```cpp
#if CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM
                ESP_LOGI(TAG, "C6 listening: voice uplink enabled, mode=%d", listening_mode_);
#endif
```

- [ ] **步骤 3：构建确认没有破坏编译**

运行：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
"/Users/jiangzhibin/.espressif/tools/ninja/1.12.1/ninja" -C "/Users/jiangzhibin/workspace/xiaozhi-esp32/build-muselab-nanoesp32-c6-pdm"
```

预期：

```text
ninja: no work to do.
```

或：

```text
[...]
```

且最终没有 `FAILED`。

## 任务 2：把 C6 的 `tts.stop` 改为限时续聊窗口

**文件：**
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc`

- [ ] **步骤 1：定位当前 C6 `tts.stop` 行为**

运行：

```bash
sed -n '536,565p' "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc"
```

当前 C6 特化行为应类似：

```cpp
#if CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM
                        SetDeviceState(kDeviceStateIdle);
#else
```

- [ ] **步骤 2：改为进入 `Listening` 并启动自动停止窗口**

将 C6 分支改为下面的语义。关键点：如果播放期 barge-in 已经让 audio processor 保持运行，`Listening` 状态处理器不会重新发送 `listen.start`，所以这里要先把服务端 mode 切回 `auto`；如果 audio processor 没运行，则不要在这里发送，交给 `Listening` 状态处理器发送，避免重复 `listen.start`。

```cpp
#if CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM
                        listening_mode_ = kListeningModeAutoStop;
                        if (audio_service_.IsAudioProcessorRunning()) {
                            protocol_->SendStartListening(listening_mode_);
                        }
                        SetDeviceState(kDeviceStateListening);
#else
```

理由：

```text
Listening 状态已在 C6 + AutoStop 模式调用 StartAutoStopListeningTimer()
4.5 秒固定窗口到期时 StopListening() 会发送 listen.stop 并回 Idle
当前 NoAudioProcessor 不提供有效 VAD，固件不会因为用户正在说话自动续期
```

- [ ] **步骤 3：确认 `Listening` 分支只在 AutoStop 启动计时器**

检查：

```bash
sed -n '913,948p' "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc"
```

预期包含：

```cpp
#if CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM
            if (listening_mode_ == kListeningModeAutoStop) {
                StartAutoStopListeningTimer();
            }
#endif
```

- [ ] **步骤 4：构建**

运行：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
"/Users/jiangzhibin/.espressif/tools/ninja/1.12.1/ninja" -C "/Users/jiangzhibin/workspace/xiaozhi-esp32/build-muselab-nanoesp32-c6-pdm"
```

预期：构建成功。

## 任务 3：新增播放期打断上行协议标记与安全上行入口

**文件：**
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio/audio_service.h`
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio/audio_service.cc`
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/protocols/protocol.h`
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/protocols/protocol.cc`
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc`

- [ ] **步骤 1：扩展 `ListeningMode`**

在 `protocol.h` 中把枚举扩展为：

```cpp
enum ListeningMode {
    kListeningModeAutoStop,
    kListeningModeManualStop,
    kListeningModeRealtime,
    kListeningModeBargeIn
};
```

- [ ] **步骤 2：扩展 `Protocol::SendStartListening` 的 mode 文本**

在 `protocol.cc` 中改为：

```cpp
const char* mode_text = mode == kListeningModeRealtime ? "realtime" :
    mode == kListeningModeAutoStop ? "auto" :
    mode == kListeningModeBargeIn ? "barge_in" : "manual";
ESP_LOGI(TAG, "Send listen start, mode=%s", mode_text);
std::string message = "{\"session_id\":\"" + session_id_ + "\"";
message += ",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"";
message += mode_text;
message += "\"}";
SendText(message);
```

保留现有 `session_id` 或其他字段逻辑，不改变非 C6 模式。

- [ ] **步骤 3：给 `AudioService` 增加不清 decoder 的启动入口**

不要在 `Speaking` 中直接调用 `EnableVoiceProcessing(true)`。先在 `audio_service.h` 声明：

```cpp
void EnableVoiceProcessingForBargeIn();
```

并在 private 区域声明一个复用 helper：

```cpp
void StartVoiceProcessing(bool reset_decoder);
```

在 `audio_service.cc` 中把 `EnableVoiceProcessing(true)` 的启动逻辑抽成 helper，保持默认入口仍会 reset decoder：

```cpp
void AudioService::StartVoiceProcessing(bool reset_decoder) {
    if (!audio_processor_initialized_) {
        audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
        audio_processor_initialized_ = true;
    }

    if (reset_decoder) {
        ResetDecoder();
    }
    audio_input_need_warmup_ = true;
    {
        std::lock_guard<std::mutex> lock(input_resampler_mutex_);
        if (input_resampler_ != nullptr) {
            esp_ae_rate_cvt_reset(input_resampler_);
        }
    }
    audio_processor_->Start();
    xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
}
```

然后把 `EnableVoiceProcessing()` 改为：

```cpp
void AudioService::EnableVoiceProcessing(bool enable) {
    ESP_LOGD(TAG, "%s voice processing", enable ? "Enabling" : "Disabling");
    if (enable) {
        StartVoiceProcessing(true);
    } else {
        audio_processor_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    }
}
```

新增播放期入口：

```cpp
void AudioService::EnableVoiceProcessingForBargeIn() {
    ESP_LOGD(TAG, "Enabling voice processing for barge-in");
    StartVoiceProcessing(false);
}
```

必须确认 `EnableVoiceProcessingForBargeIn()` 不调用 `ResetDecoder()`，否则会清空正在播放的 TTS。

- [ ] **步骤 4：在播放期进入上行窗口时发送 `barge_in`**

在 C6 `Speaking` 分支中增加受控上行。C6 分支进入 `Speaking` 时可以先清理上一轮残留 decoder 队列，但随后必须通过 `EnableVoiceProcessingForBargeIn()` 启动上行，不能再调用会清空播放队列的 `EnableVoiceProcessing(true)`：

```cpp
#if CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM
            audio_service_.ResetDecoder();
            protocol_->SendStartListening(kListeningModeBargeIn);
            audio_service_.EnableVoiceProcessingForBargeIn();
            audio_service_.EnableWakeWordDetection(false);
#else
            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
#endif
```

注意：`audio_service_.ResetDecoder();` 只保留在 C6 `Speaking` 分支进入时和非 C6 原路径中；不要在 `EnableVoiceProcessingForBargeIn()` 内部 reset。

- [ ] **步骤 5：避免非 C6 行为变化**

运行：

```bash
rg -n "kListeningModeBargeIn|EnableVoiceProcessingForBargeIn|CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM" "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc" "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/audio" "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/protocols"
```

预期：

```text
kListeningModeBargeIn 只在协议枚举、mode 文本和 C6 Speaking/模式切换路径出现
EnableVoiceProcessingForBargeIn 只在 AudioService 声明/实现和 C6 Speaking 分支出现
```

- [ ] **步骤 6：构建**

运行：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
"/Users/jiangzhibin/.espressif/tools/ninja/1.12.1/ninja" -C "/Users/jiangzhibin/workspace/xiaozhi-esp32/build-muselab-nanoesp32-c6-pdm"
```

预期：构建成功。

## 任务 4：打断后按触发来源选择回 Idle 或续聊窗口

**文件：**
- 修改：`/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc`

- [ ] **步骤 1：审查 `AbortSpeaking()`**

运行：

```bash
sed -n '999,1012p' "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc"
```

当前应包含：

```cpp
aborted_ = true;
audio_service_.ResetDecoder();
protocol_->SendAbortSpeaking(reason);
```

- [ ] **步骤 2：不要在 `AbortSpeaking()` 内无条件进入 follow-up**

保留 `AbortSpeaking()` 的职责为“清播放并通知服务端 abort”。不要在函数末尾直接追加：

```cpp
SetDeviceState(kDeviceStateListening);
```

理由：

```text
BOOT 点击播放中调用 ToggleChatState() -> AbortSpeaking(kAbortReasonNone)，用户语义是停止，不应被强制进入自动续聊。
wake word 打断和播放期 barge-in 需要续聊，但应在对应调用点设置状态。
```

- [ ] **步骤 3：播放中 BOOT 手动停止应立即回 Idle**

在 `HandleToggleChatEvent()` 的 `state == kDeviceStateSpeaking` 分支中，为 C6 增加手动停止语义：

```cpp
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
#if CONFIG_BOARD_TYPE_MUSELAB_NANOESP32_C6_PDM
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
#endif
```

非 C6 保持原行为。

- [ ] **步骤 4：唤醒词打断仍进入默认续聊路径**

检查 `HandleWakeWordDetectedEvent()` 中 `state == kDeviceStateSpeaking || state == kDeviceStateListening` 的现有逻辑。`Speaking` 分支当前通过：

```cpp
play_popup_on_listening_ = true;
SetListeningMode(GetDefaultListeningMode());
```

进入默认 `Listening`。C6 默认 `GetDefaultListeningMode()` 在 `kAecOff` 下是 `kListeningModeAutoStop`，这一路应保留。

- [ ] **步骤 5：确认 incoming audio ignore 逻辑仍保留**

运行：

```bash
rg -n "aborted_|incoming audio|OnIncomingAudio" "/Users/jiangzhibin/workspace/xiaozhi-esp32/main/application.cc"
```

预期：播放被打断后旧下行音频不会继续喂给 decoder。

- [ ] **步骤 6：构建**

运行：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
"/Users/jiangzhibin/.espressif/tools/ninja/1.12.1/ninja" -C "/Users/jiangzhibin/workspace/xiaozhi-esp32/build-muselab-nanoesp32-c6-pdm"
```

预期：构建成功。

## 任务 5：串口与服务端联调验收

**文件：**
- 不修改文件
- 验证：串口 monitor、服务端 WebSocket 日志

- [ ] **步骤 1：刷机**

串口按实际设备为准，之前本机出现过 `/dev/cu.usbmodem101`：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
idf.py -B "build-muselab-nanoesp32-c6-pdm" -p "/dev/cu.usbmodem101" flash
```

如果加载 ESP-IDF 环境后 `idf.py` 仍不可用，先停止联调并修复本机工具链；不要改用未加载 `IDF_PATH` 的裸 `ninja`。

- [ ] **步骤 2：待机不上行验证**

观察串口：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
idf.py -B "build-muselab-nanoesp32-c6-pdm" -p "/dev/cu.usbmodem101" monitor
```

预期：

```text
C6 idle: wake word local only, voice uplink disabled
```

并且 10 秒以上不持续出现：

```text
WS: Sent audio packets
```

- [ ] **步骤 3：唤醒后上行验证**

说唤醒词后预期串口：

```text
Wake word detected
C6 listening: voice uplink enabled, mode=0
Send listen start, mode=auto
WS: Sent audio packets
```

服务端预期：

```text
xiaozhi listen started ... mode=auto
```

如果后续显式启用 `CONFIG_SEND_WAKE_WORD_DATA`，服务端还可能看到 `listen.detect`；当前 C6 `CONFIG_USE_ESP_WAKE_WORD` 路径不要求它出现。

- [ ] **步骤 4：回答结束后续聊窗口验证**

服务端回复结束后预期串口：

```text
tts.stop
Send listen start, mode=auto
```

4.5 秒内服务端没有进入下一轮 `tts.start` 时预期：

```text
Send listen stop
C6 idle: wake word local only, voice uplink disabled
```

- [ ] **步骤 5：播放期打断上行验证**

在 TTS 播放中说“停一下”或新问题。服务端预期先看到：

```text
xiaozhi listen started ... mode=barge_in
```

随后如果服务端方案已实现，预期下发取消 TTS 并进入新一轮续聊窗口。

## 风险与控制

- C6 没有本地 AEC，播放期上传的音频会混入设备扬声器声音。固件不能单独解决，需要服务端用 ASR 文本、音量门限、最短语音时长、冷却时间和 TTS 文本相似度过滤。
- `EnableVoiceProcessing(true)` 会 `ResetDecoder()`，如果在 `Speaking` 中直接调用会清空 TTS 播放队列。实施必须使用 `EnableVoiceProcessingForBargeIn()` 这类不 reset decoder 的轻量入口。
- `barge_in` mode 是新增协议标记，服务端未实现前只能看到日志和音频帧，不会真正打断。
- 4.5 秒窗口是第一版经验值；太短影响连续对话，太长增加无效上行。上线后应根据串口和服务端日志调整为配置项。
- 当前工作区已有未提交改动，实施时只改上述文件，不回滚用户现有变更。

## 自检清单

- [ ] `Idle` 不启用 `AudioProcessor`。
- [ ] `Listening` 只在唤醒、手动开始、续聊窗口内启用上行。
- [ ] `Speaking` 只对 C6 启用 `barge_in` 上行，不影响其他板型。
- [ ] `tts.stop` 后不是永久监听，而是 4.5 秒限时窗口。
- [ ] follow-up 窗口是固定 4.5 秒，不宣称本地 VAD 可自动续期。
- [ ] BOOT 播放中手动停止回 `Idle`，不被 `AbortSpeaking()` 强制改成续聊。
- [ ] 自动超时后回到 `Idle` 并停止音频上行。
- [ ] 构建通过。
