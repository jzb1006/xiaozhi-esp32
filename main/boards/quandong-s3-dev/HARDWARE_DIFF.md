# quandong-s3-dev vs bread-compact-wifi 硬件差异

两块板都基于 **ESP32-S3 + WiFi**，软件层都继承 `WifiBoard`，启动 → 配网 → 接小智后端 → 语音对话 的主流程完全一致。差异集中在 **音频路径**、**显示**、**人机交互** 三块。

---

## 1. 概览

| 维度 | quandong-s3-dev | bread-compact-wifi |
|---|---|---|
| 芯片 | ESP32-S3 | ESP32-S3 |
| 基类 | `WifiBoard` | `WifiBoard` |
| 音频 codec | **ES8311 硬 codec**（I2C 控制 + I2S 全双工） | **无 codec**：MEMS I2S 麦克 + I2S 数字功放（`NoAudioCodec`） |
| 显示 | **ILI9341 240×320 SPI 彩屏** | **GC9A01 1.28 寸 240×240 SPI 圆形 IPS 彩屏** |
| 按键数 | 1（BOOT） | 4（BOOT、Touch、Vol+、Vol-） |
| 板载 LED | 无 | GPIO48 单色 |
| MCP 外设示例 | 无 | `LampController`（GPIO18） |
| 背光 | PWM on GPIO45 | BLC / BLK 接 3V3 常亮 |
| 字体 / emoji 资源 | `font_noto_basic_20_4` + `font_awesome_20_4` + `noto-emoji_128` | `font_puhui_basic_14_1` + `font_awesome_14_1`（无 emoji） |

---

## 2. 音频子系统

### quandong-s3-dev

| 项 | 值 |
|---|---|
| Codec 芯片 | **ES8311**（I2C 地址 `ES8311_CODEC_DEFAULT_ADDR`） |
| I2C 总线 | I2C0，SDA = **GPIO16**，SCL = **GPIO15** |
| I2S 模式 | **Duplex**（一套 I2S 复用） |
| I2S 引脚 | MCLK = **GPIO4**，BCLK = **GPIO5**，WS = **GPIO7**，DIN = **GPIO6**，DOUT = **GPIO8** |
| 采样率 | 输入 / 输出 = **24000 / 24000** |
| 功放控制 | **GPIO1 拉低使能**（由 `InitializeAudioPaEnable()` 处理） |
| 音量控制 | 通过 ES8311 寄存器（`Es8311AudioCodec::SetOutputVolume`） |

### bread-compact-wifi

| 项 | 值 |
|---|---|
| Codec 芯片 | **无**（用 `NoAudioCodecSimplex`） |
| I2S 模式 | **Simplex**（麦克和喇叭走两套 I2S） |
| 麦克 I2S | WS = GPIO4，SCK = GPIO5，DIN = GPIO6 |
| 喇叭 I2S | DOUT = GPIO7，BCLK = GPIO15，LRCK = GPIO16 |
| 采样率 | 输入 / 输出 = **16000 / 24000** |
| 音量控制 | I2S 数字增益（软件实现） |

### 关键差异

- **硬件 codec vs 纯 I2S**：ES8311 提供更稳定的 ADC/DAC、硬件音量、消除 PoP，bread-compact-wifi 依赖软件处理。
- **采样率**：quandong 麦克按 24kHz 采，bread-compact-wifi 是 16kHz。两者都被 audio pipeline 重采样到协议层需要的速率。
- **显示总线**：quandong 用 SPI2 驱动 ILI9341；bread-compact-wifi 用 SPI3 驱动 GC9A01。两块板的显示控制器、SPI host 和引脚都不同。

---

## 3. 显示子系统

### quandong-s3-dev

| 项 | 值 |
|---|---|
| 屏型号 | **ILI9341** |
| 接口 | **SPI**（SPI2，40 MHz） |
| 分辨率 | **240 × 320**，配置为横屏使用（`SWAP_XY=true` → 实际 320×240） |
| 引脚 | MOSI = GPIO11，SCK = GPIO12，CS = GPIO10，DC = GPIO46 |
| 背光 | **PWM 控制**，GPIO45，非反相 |
| 显示类 | `SpiLcdDisplay`（LVGL 9，RGB565） |
| 初始化序列 | 板厂自定义 `ili9341_vendor_specific_init`（gamma / power 时序） |

### bread-compact-wifi

| 项 | 值 |
|---|---|
| 屏型号 | **GC9A01** |
| 接口 | **SPI**（SPI3，10 MHz） |
| 分辨率 | **240 × 240** |
| 引脚 | MOSI/SDA = GPIO41，SCLK/SCL = GPIO42，DC = GPIO21，CS = GPIO38，RST = GPIO17 |
| 背光 | BLC / BLK 接 3V3 常亮 |
| 显示类 | `SpiLcdDisplay`（RGB565） |

### 关键差异

- **分辨率 / 形态**：quandong 是 240×320 矩形屏；bread-compact-wifi 是 240×240 圆屏。
- **接口带宽**：两者都走 SPI，quandong 当前配置 40 MHz，bread-compact-wifi 当前配置 10 MHz。
- **驱动栈**：quandong 用 `esp_lcd_ili9341` (component 1.2.0) + `lvgl_port`；bread-compact-wifi 用 `esp_lcd_gc9a01` + `SpiLcdDisplay`。

---

## 4. 输入与状态指示

### 按键

| 按键 | quandong | bread-compact-wifi | 行为 |
|---|---|---|---|
| BOOT (GPIO0) | ✅ | ✅ | 启动期进配网；运行时 `ToggleChatState`（按一下开/关对话） |
| Touch (GPIO47) | ❌ | ✅ | **按住说话**：`OnPressDown→StartListening`, `OnPressUp→StopListening` |
| Vol+ (GPIO40) | ❌ | ✅ | 单击 +10 音量；长按拉满 100 |
| Vol- (GPIO39) | ❌ | ✅ | 单击 -10 音量；长按静音 |

→ quandong 只有 BOOT 一个键，**没有按住说话**，**没有物理音量键**。调音量需要走 MCP / 语音指令。

### LED

| 板子 | LED | 用途 |
|---|---|---|
| quandong | `BUILTIN_LED_GPIO = NC` | 无 |
| bread-compact-wifi | GPIO48，`SingleLed` | 状态指示（idle / listening / speaking） |

### MCP 工具

bread-compact-wifi 注册了一个 `LampController(GPIO18)` 作为 MCP 协议演示，可以远程通过小智指令"开灯/关灯"。quandong 没有这种外设示例。

---

## 5. 启动流程差异

主流程相同（详见 `application.cc:61` / `wifi_board.cc:52`），UI 也由 `Application::Initialize()` 统一拉起。差别只在 **board 构造里做的事**：

### quandong-s3-dev 构造序列

```
InitializeI2c()             // I2C0 for ES8311
InitializeSpi()             // SPI2 for ILI9341
InitializeAudioPaEnable()   // GPIO1 拉低使能功放
InitializeIli9341Display()  // 自定义 init cmds + 实例化 SpiLcdDisplay
InitializeButtons()         // BOOT 单键
GetBacklight()->SetBrightness(100)
```

### bread-compact-wifi 构造序列

```
InitializeSpi()             // SPI3 for GC9A01
InitializeGc9a01Display()   // SpiLcdDisplay
InitializeButtons()         // BOOT + Touch + Vol+ + Vol-
InitializeTools()           // 注册 LampController（MCP 外设）
```

---

## 6. 资源与构建配置

`main/CMakeLists.txt` 中的字体与 emoji 配置差异：

| 板子 | text font | icon font | emoji collection |
|---|---|---|---|
| quandong-s3-dev | `font_noto_basic_20_4` | `font_awesome_20_4` | `noto-emoji_128` |
| bread-compact-wifi | `font_puhui_basic_14_1` | `font_awesome_14_1` | （无） |

→ quandong 走的是和 `lichuang-dev`、`esp-box-3` 等 240×320 LCD 板同档资源；bread-compact-wifi 当前虽已接入 240×240 GC9A01 圆屏，但字体和 emoji 资源仍沿用轻量配置。

### 其它依赖

- quandong 需要 `espressif/esp_lcd_ili9341 ==1.2.0`（仓库 `main/idf_component.yml` 已有，无需补依赖）
- bread-compact-wifi 使用 `esp_lcd_gc9a01`

---

## 7. 一句话总结

> **quandong-s3-dev** ≈ "彩屏 + 硬 codec + 单按键" 的工程开发板，UI 表现力强但交互简化。
> **bread-compact-wifi** ≈ "GC9A01 圆屏 + 纯 I2S 软 codec + 多按键 + 板载 lamp" 的面包板参考实现，便于上手 MCP 外设。
> 功能层（配网、语音通话、MCP、唤醒）由 `WifiBoard` 基类统一提供，两者表现一致。
