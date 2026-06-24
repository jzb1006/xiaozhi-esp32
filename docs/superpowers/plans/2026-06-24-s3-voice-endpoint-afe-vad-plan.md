# ESP32-S3 语音端点检测与低延迟方案

> **面向 AI 代理的工作者：** 本文是方案文档，不是执行记录。未经用户明确确认，不执行代码修改、构建、刷机、提交或推送。

**目标：** 基于当前 ESP32-S3 硬件方案，解决“下达命令到返回结果时间太长”的问题，重点避免语音回合因端点检测失败拖到服务端最大时长。

**当前硬件：** ESP32-S3-WROOM-1-N16R8，固件板型 `bread-compact-wifi`，麦克风 INMP441，功放 MAX98357A。

**当前边界：** 后续不再按 MuseLab C6 兼容路线设计。C6 的固定 4.5 秒本地停止录音逻辑不再作为约束。

---

## 关键结论

- 当前 S3 方案应采用“设备侧 AFE/VAD 前置处理 + 服务端 auto-stop 兜底断句”的路线。
- 不建议第一阶段直接上完整设备侧 AEC。`bread-compact-wifi` 是 DIY I2S 麦克风和功放方案，当前不在 `USE_DEVICE_AEC` 默认白名单内；AEC 需要干净的播放参考路径和较好的麦克风/喇叭物理隔离。
- 不建议第一阶段把 WebRTC VAD 或 Silero VAD 放在服务端作为主路径。设备侧离麦克风最近，能更早处理噪声、唤醒和语音活动；服务端 VAD 更适合作为后续增强或兜底。
- 服务端仍必须保留 auto-stop 保护，避免固件、网络或 ASR endpoint 失效时无限等待。
- 线上验收不能只看“能回答”，必须看 `asrMillis`、`auto-stop reason`、`userText` 和 `ttsMillis`。

## 当前代码锚点

- `docs/hardware/esp32-s3-n16r8-inmp441-max98357a-2026-06-23.md`
  - 记录当前 S3 硬件、接线、固件板型和烧录信息。
- `main/boards/bread-compact-wifi/config.h`
  - INMP441：`GPIO4` / `GPIO5` / `GPIO6`。
  - MAX98357A：`GPIO7` / `GPIO15` / `GPIO16`。
  - 当前使用 `AUDIO_I2S_METHOD_SIMPLEX`。
- `main/Kconfig.projbuild`
  - S3 + PSRAM 默认支持 `USE_AFE_WAKE_WORD`。
  - S3 + PSRAM 支持 `USE_AUDIO_PROCESSOR`。
  - `USE_DEVICE_AEC` 当前限定在部分成熟板型，不应直接假设 `bread-compact-wifi` 已具备稳定 AEC 条件。
- `main/application.cc`
  - 已移除旧 C6 固件的 4.5 秒本地固定截断路径。
- `main/application.cc`
  - S3 唤醒后进入 listening，由服务端根据音频流判断一句话结束。

## 推荐架构

```text
ESP32-S3 固件
  -> AFE wake word / AudioProcessor / VAD 观测
  -> 唤醒后持续上传 Opus 音频
  -> 不在固件侧使用固定 4.5 秒截断

Java voice-gateway
  -> 接收流式音频
  -> auto-stop 端点检测兜底
  -> 完成 ASR stream
  -> 调 Hermes
  -> TTS 播放

Sherpa ASR sidecar
  -> streaming ASR
  -> 后续优先核查 endpointing 能力
```

职责划分：

- 固件负责音频采集、唤醒、基础语音活动观测和音频上行。
- Java 服务端负责当前实际断句、ASR/TTS 编排和日志观测。
- Hermes 负责语义、工具调用和回答内容。
- Sherpa ASR 后续可承接更正统的 ASR endpointing，但不能在未验证前替代服务端兜底。

## 分阶段方案

### 阶段 1：建立 S3 现状基线

目标是确认“慢”是否仍来自端点检测，而不是 Hermes 或 TTS。

检查项：

- S3 固件已烧录 `origin/main` 的 `0e71bcd4` 或后续 S3 分支。
- 设备在线后可以正常唤醒、进入 listening、上传音频。
- 服务端最新日志中能看到：
  - `xiaozhi wake word detected`
  - `xiaozhi listen started`
  - `xiaozhi auto-stop detected ...`
  - `xiaozhi turn completed ... asrMillis=... hermesMillis=... ttsMillis=...`
- 正常短命令不应再稳定出现 `asrMillis=60000`。

建议测试语句：

- “现在几点？”
- “讲个笑话。”
- “帮我介绍一下今天适合做什么。”
- 唤醒后不说话。
- 说一句话，中间停顿 1 秒，再继续。

### 阶段 2：服务端 auto-stop 参数先收敛

这是当前最小可控改动面。

建议目标：

- 正常短命令：`asrMillis < 8000`。
- 空唤醒：走 `NO_SPEECH_TIMEOUT`，不要进入 Hermes。
- 长句：允许稍长，但不应触发 `MAX_DURATION_REACHED=60000`。
- 最大句长兜底建议先降到 `10s-15s`。

服务端配置方向：

```text
chatbot.voice.auto-stop.no-speech-timeout = 5s-8s
chatbot.voice.auto-stop.max-duration = 10s-15s
chatbot.voice.auto-stop.silence-duration = 800ms-1200ms
chatbot.voice.auto-stop.speech-rms-threshold = 按现场噪声校准
```

如果日志仍显示 `reason=MAX_DURATION_REACHED` 且 `peakRms` 明显偏高，优先处理端点检测算法，而不是继续调大最大时长。

### 阶段 3：固件侧增加 VAD / RMS 观测日志

目标是让 S3 固件给出可解释的音频活动证据，而不是只依赖服务端最终耗时。

建议日志字段：

```text
vadActive
rms
peakRms
noiseFloor
listeningMode
audioProcessorRunning
wakeWordDetected
uplinkEnabled
```

日志频率要受控：

- 不逐帧打印。
- 每 500 ms 或每 1 s 聚合打印一次。
- 状态切换时立即打印。

成功标准：

- 待机时能看到 wake word 相关状态，但不持续上行普通音频。
- 唤醒后能看到语音活动变化。
- 用户停止说话后，VAD 或 RMS 观测能出现下降窗口。

### 阶段 4：启用并校准 S3 AFE / AudioProcessor 能力

目标是让设备侧更稳定地处理噪声和唤醒，不把所有判断压力都放到服务端。

检查项：

- 构建产物中确认启用了 `CONFIG_USE_AFE_WAKE_WORD`。
- 构建产物中确认启用了 `CONFIG_USE_AUDIO_PROCESSOR`。
- 确认 `AudioService::IsAfeWakeWord()` 在当前 S3 板型运行时返回符合预期。
- 确认 listening 状态下是否需要开启 `WAKE_WORD_DETECTION_IN_LISTENING`。

暂不启用完整设备侧 AEC，除非满足以下条件：

- 现场出现明确的 TTS/音乐自听问题。
- 串口或服务端日志显示播放期间麦克风把本机声音送回 ASR。
- 能提供稳定的播放参考路径。
- 麦克风和喇叭有足够物理隔离。

### 阶段 5：核查 Sherpa endpointing 能力

目标是把“什么时候一句话结束”逐步从简单能量判断升级到 ASR-aware endpointing。

建议核查：

- 当前 `sherpa-onnx` sidecar 是否支持 endpointing 参数。
- 是否能配置类似规则：
  - 无有效语音 5 秒超时。
  - 已有识别内容后，尾部静音 0.8 秒到 1.2 秒结束。
  - 最大句长 12 秒到 15 秒兜底。
- 是否能向 Java 返回 endpoint 事件，或者通过 stream close / final result 表达回合结束。

如果 Sherpa endpointing 可用，推荐方向：

```text
S3 固件负责稳定上行
  -> Sherpa 判断语音内容和 endpoint
  -> Java auto-stop 只保留 no-speech / max-duration 兜底
```

### 阶段 6：后续再评估服务端 VAD

只有当 S3 AFE/VAD + Sherpa endpointing 仍不能满足现场噪声时，再考虑服务端 VAD。

可选方案：

- WebRTC VAD：轻量、成熟，适合实时二分类。
- Silero VAD：神经网络 VAD，抗噪能力更强，但集成成本更高。

推荐接入方式：

- 先做 sidecar，不要直接把复杂 native 依赖塞进 Java 主服务。
- Java 只消费 `VOICE` / `SILENCE` / `END_OF_UTTERANCE` 这类稳定事件。
- 保留现有 auto-stop 参数作为兜底。

## 成功标准

- 短命令从唤醒后到开始回复不再出现 60 秒级等待。
- 服务端最新真实日志中，正常语音回合不再稳定走 `MAX_DURATION_REACHED`。
- 空唤醒能走 `NO_SPEECH_TIMEOUT` 或等价空输入处理，不进入 Hermes 生成回答。
- 长句不会被固定 4.5 秒截断。
- 设备播放 TTS 后再次唤醒可进入 listening。
- 若用户说话中间短暂停顿，系统不会过早截断。
- 若用户已经说完，系统不会继续等到最大时长。

## 验证命令与日志

远程服务端日志建议按以下关键词查：

```text
xiaozhi websocket connected
xiaozhi wake word detected
xiaozhi listen started
xiaozhi auto-stop armed
xiaozhi auto-stop detected
xiaozhi turn completed
xiaozhi streaming conversation turn
xiaozhi streaming asr returned blank text
```

重点指标：

```text
reason
audioMillis
peakRms
speechStarted
asrMillis
hermesMillis
ttsMillis
sentenceCount
ttsFrames
userText
assistantText
```

判断规则：

- `asrMillis` 接近 `max-duration`：端点检测仍有问题。
- `hermesMillis` 高：语义、工具调用或模型慢。
- `ttsMillis` 高且 `sentenceCount` 多：回复太长或 TTS 首帧/合成慢。
- `userText` 为空：音频上行、ASR 或端点切分需要继续查。

## 非目标

- 不再维护 C6 方案兼容性。
- 不恢复 C6 的 4.5 秒固定本地 auto-stop。
- 不在固件里做中文语义判断。
- 不把播放音乐当作 TTS 路径处理。
- 不在第一阶段强行启用设备侧 AEC。
- 不把服务端 VAD 作为第一优先级。
- 不提交、不推送、不刷机，除非用户明确要求。

## 推荐下一步

1. 查一轮 S3 实机测试后的服务端日志，确认 `asrMillis` 和 `auto-stop reason`。
2. 如果仍有 `MAX_DURATION_REACHED`，先收敛服务端 auto-stop 参数。
3. 同步补固件侧 VAD/RMS 观测日志。
4. 再核查 Sherpa endpointing 能力。
5. 最后根据播放期自听情况决定是否进入 AEC 方案。
