# MuseLab C6 音量控制设备侧方案

> **面向 AI 代理的工作者：** 本文是方案文档，不是实现计划。未经用户确认，不执行代码修改、构建、刷机、提交或推送。

**目标：** 明确 MuseLab nanoESP32-C6 固件侧如何承接“语音控制音量”能力，并把固件职责限定为设备 MCP 工具、音频增益执行和本地持久化。

**架构：** 音量语义由 Hermes 处理，Java 服务端通过小智 MCP 薄桥调用设备工具；固件只暴露 `self.get_device_status` 与 `self.audio_speaker.set_volume`，并在 `AudioCodec` 中持久化 `output_volume`。MuseLab C6 当前没有独立音量按键 GPIO，第一版不新增按键交互。

**技术栈：** ESP-IDF 5.5.x、C++17、小智 MCP JSON-RPC、`Settings` NVS 存储、`AudioCodec`、`NoAudioCodec` I2S 输出。

---

## 关键结论

- 固件侧音量控制基础已经存在，不需要新增底层音频通路。
- `main/mcp_server.cc` 已注册 `self.get_device_status` 和 `self.audio_speaker.set_volume`。后者参数范围为 `0-100`，调用 `AudioCodec::SetOutputVolume(...)`。
- `AudioCodec::SetOutputVolume(...)` 会写入 `Settings("audio", true)` 的 `output_volume`，重启后由 `AudioCodec::Start()` 读取。
- `NoAudioCodec::Write(...)` 会按 `output_volume_` 对输出 PCM 做平方曲线缩放，因此 MuseLab C6 的 I2S 功放输出能实际受音量影响。
- MuseLab C6 当前 `VOLUME_UP_BUTTON_GPIO` 和 `VOLUME_DOWN_BUTTON_GPIO` 都是 `GPIO_NUM_NC`，第一版不做物理音量键。
- 设备端不解析“大一点、小一点、静音”等自然语言，也不保存“上次非零音量”这类语义状态；这些属于 Hermes 编排层。

## 设备侧职责边界

固件负责：

- 暴露稳定设备 MCP 工具。
- 校验 `volume` 参数类型和范围，保持 `0-100` 百分比语义。
- 应用音量到当前播放输出。
- 持久化 `output_volume`，确保重启后保持用户设置。
- 通过 `self.get_device_status` 返回当前音量，供 Hermes 做相对调节。

固件不负责：

- 不判断用户意图。
- 不根据中文短语计算音量增减。
- 不实现独立管理接口。
- 不新增设备端复杂状态，例如“上次静音前音量”。
- 不改非 MuseLab C6 板型默认行为。

## 现有代码锚点

- `main/mcp_server.cc`
  - `self.get_device_status`：返回设备状态，包含 `audio_speaker.volume`。
  - `self.audio_speaker.set_volume`：接收 `volume` 参数并调用 `codec->SetOutputVolume(...)`。
- `main/audio/audio_codec.cc`
  - `AudioCodec::Start()`：读取 `audio.output_volume`。
  - `AudioCodec::SetOutputVolume(...)`：更新 `output_volume_` 并持久化。
- `main/audio/codecs/no_audio_codec.cc`
  - `NoAudioCodec::Write(...)`：按 `output_volume_` 缩放输出采样。
- `main/boards/muselab-nanoesp32-c6-pdm/config.h`
  - `AUDIO_DEFAULT_VOLUME 60`。
  - `VOLUME_UP_BUTTON_GPIO GPIO_NUM_NC`。
  - `VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC`。
- `main/boards/muselab-nanoesp32-c6-pdm/muselab_nanoesp32_c6_pdm_board.cc`
  - `InitializeDefaultVolume()`：首次启动写入默认音量，不覆盖已保存的 `output_volume`。

## 推荐方案

第一版采用“设备 MCP 工具承接”的方案：

```text
Hermes 意图识别
  -> Java MCP 网关 xiaozhi_call_device_tool
  -> 固件 self.audio_speaker.set_volume
  -> AudioCodec::SetOutputVolume
  -> Settings 持久化 output_volume
  -> NoAudioCodec::Write 输出缩放
```

固件侧只需确认当前能力可用；除非验证发现工具列表缺失、参数未生效或音量边界异常，否则不改固件代码。

## 交互约定

Hermes 对用户语音做归一化，固件只接收最终数值：

- “音量调到 60” -> `volume=60`
- “大一点” -> 先读当前音量，再 `min(current + 10, 100)`
- “小一点” -> 先读当前音量，再 `max(current - 10, 0)`
- “静音” -> `volume=0`
- “恢复声音” -> 第一版建议 `volume=50` 或由 Hermes 记忆恢复上次非零值

设备工具调用参数固定为：

```json
{
  "name": "self.audio_speaker.set_volume",
  "arguments": {
    "volume": 60
  }
}
```

## 成功标准

- 设备在线后，服务端通过 MCP `tools/list` 能看到 `self.audio_speaker.set_volume`。
- 调用 `self.get_device_status` 能读到当前 `audio_speaker.volume`。
- 调用 `self.audio_speaker.set_volume` 设置 `30` 后，设备日志出现 `Set output volume to 30`。
- 再次调用 `self.get_device_status` 返回音量为 `30`。
- 播放 TTS 或音乐时，实际扬声器音量明显变化。
- 设备重启后，`output_volume` 仍保持最后一次设置值；如果 NVS 为空，首次启动默认值为 `60`。

## 验证步骤

1. 确认固件构建环境：

```bash
source "/Users/jiangzhibin/esp/esp-idf-5.5.2/export.sh"
idf.py -B "build-muselab-nanoesp32-c6-pdm" -DSDKCONFIG="sdkconfig.muselab-nanoesp32-c6-pdm" build
```

2. 设备连上服务端后，从服务端侧列出设备工具。

3. 通过 Java MCP 网关调用：

```json
{
  "deviceId": "${ONLINE_DEVICE_ID}",
  "name": "self.audio_speaker.set_volume",
  "arguments": {
    "volume": 30
  }
}
```

4. 串口监控重点看：

```text
Set output volume to 30
```

5. 重启后再读 `self.get_device_status`，确认音量持久化。

## 风险与处理

- 如果 `volume=0` 被 `AudioCodec::Start()` 读取后重置为 `10`，则“静音后重启保持静音”当前不成立。第一版可以接受运行态静音；若产品要求重启保持静音，需要单独修改 `AudioCodec::Start()` 的小音量保护逻辑。
- 如果 Hermes 调用相对调节时不知道当前音量，必须先调用 `self.get_device_status`，不要直接猜测。
- 如果设备不在线，Java MCP 网关应返回设备离线错误，固件侧不做补偿。
- 如果实际音量变化不明显，优先检查 TTS/音乐播放源、功放供电和 `NoAudioCodec::Write(...)` 输出路径，不先改 MCP。

## 非目标

- 不新增实体音量键。
- 不做屏幕音量 UI。
- 不修改音频采样率、Opus、PDM 或 I2S 引脚。
- 不在固件中新增中文意图规则。
- 不做提交和推送。
