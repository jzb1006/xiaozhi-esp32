# MuseLab nanoESP32-C6 音频硬件基线

更新时间：2026-06-19

## 开发板

- Board：MuseLab nanoESP32-C6 / ESP32-C6 development board
- Chip：ESP32-C6 QFN40 rev v0.2
- Flash：16 MB
- USB 串口芯片：CH343 USB-UART
- Windows 串口：COM5
- 串口波特率：115200
- esptool 观察到的 MAC：`9c:cc:01:ff:fe:40:1c:d8`
- 服务端日志常见设备 ID：`9c:cc:01:40:1c:d8`

## 麦克风

- 型号：INMP441
- 针脚数量：6 针
- 供电：3.3 V

| INMP441 | ESP32-C6 | 说明 |
| --- | --- | --- |
| VDD | 3V3 | 麦克风供电 |
| GND | GND | 共地 |
| SCK / BCLK | GPIO18 | I2S/PDM 时钟 |
| WS / LRCK | GPIO19 | I2S 左右声道时钟 |
| SD / DOUT | GPIO22 | 麦克风数据输入 |
| L/R | GND | 固定声道选择 |

## 功放

- 型号：MAX98357A
- 供电：3.3 V

| MAX98357A | ESP32-C6 | 说明 |
| --- | --- | --- |
| VIN / VCC | 3V3 | 功放供电 |
| GND | GND | 共地 |
| BCLK | GPIO18 | 与麦克风共用 BCLK |
| LRC / LRCK | GPIO19 | 与麦克风共用 LRCK |
| DIN | GPIO21 | 功放音频数据输出 |
| SD / EN | 3V3 | 使能功放 |
| GAIN | 悬空 | 使用模块默认增益 |

## 固件板型配置

- 板型目录：`main/boards/muselab-nanoesp32-c6-pdm/`
- 默认配置：`sdkconfig.defaults.muselab-nanoesp32-c6-pdm`
- 构建目标：`esp32c6`
- OTA 地址：`http://203.195.202.54:8766/api/ota/check`
- 输入采样率：16000 Hz
- 输出采样率：16000 Hz
- PDM 采样率：2048000 Hz
- 启动后自动监听：关闭
- 启动后麦克风自检：关闭

当前 GPIO 定义：

```c
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_18
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_19
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_22
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_21
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_18
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_19
```
