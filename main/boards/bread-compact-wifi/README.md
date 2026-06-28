# Bread Compact WiFi 构建和烧录说明

本板型的关键构建配置写在同目录的 `config.json` 中，包括：

- `CONFIG_OTA_URL="http://203.195.202.54:8766/api/ota/check"`

请不要直接使用裸命令 `idf.py set-target esp32s3 && idf.py build` 作为标准构建流程。裸 `idf.py build` 不会自动读取本目录的 `config.json`，容易漏掉 OTA 服务器地址等板型追加配置。

屏幕硬件参数写在同目录的 `config.h` 中。当前实物屏幕为 `GC9A01` 1.28 寸 240x240 SPI 圆形 IPS 屏，不再使用 SSD1306 OLED 构建变体。

## 环境准备

先在当前机器加载 ESP-IDF 环境，确保以下命令可以正常执行：

```powershell
idf.py --version
python --version
```

Windows 上可以打开 ESP-IDF Command Prompt，或按本机实际安装路径执行 `export.bat`。不要把某台机器的 ESP-IDF 安装目录写死到项目文档或脚本中。

## 推荐构建命令

从项目根目录执行：

```powershell
python scripts\release.py bread-compact-wifi --name bread-compact-wifi
```

该命令会读取 `main/boards/bread-compact-wifi/config.json`，并把 `sdkconfig_append` 中的配置写入本次构建生成的 `sdkconfig`。

## 推荐烧录命令

构建完成后，可以直接烧录当前构建产物：

```powershell
idf.py -p <PORT> flash
```

如果设备中可能残留旧的 NVS 配置，例如旧的 Wi-Fi 或旧的 `ota_url`，建议先整片擦除再烧录：

```powershell
idf.py -p <PORT> erase-flash flash
```

注意：`erase-flash` 会清除 Wi-Fi 配置，烧录后需要重新配网。

## 在其他机器烧录

如果只是给其他机器烧录固件，优先分发 release 包，不要在每台机器重新编译：

```text
releases/v2.2.6_bread-compact-wifi.zip
```

解压后使用其中的 `merged-binary.bin`，在目标机器执行：

```powershell
python -m esptool --chip esp32s3 -p <PORT> -b 460800 --before default_reset --after hard_reset erase_flash
python -m esptool --chip esp32s3 -p <PORT> -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 merged-binary.bin
```

把 `<PORT>` 替换成目标机器上的实际串口，例如 Windows 上的 `COM7`，macOS 上的 `/dev/cu.usbmodem*`，Linux 上的 `/dev/ttyUSB0` 或 `/dev/ttyACM0`。

## 烧录前检查

构建后建议检查 `sdkconfig` 和二进制内容，确认没有漏掉板型配置：

```powershell
Select-String sdkconfig -Pattern "CONFIG_OTA_URL|CONFIG_SR_WN_WN9_|CONFIG_USE_CUSTOM_WAKE_WORD"
Select-String main\boards\bread-compact-wifi\config.h -Pattern "GC9A01|DISPLAY_WIDTH|DISPLAY_HEIGHT|DISPLAY_MOSI_PIN|DISPLAY_CLK_PIN|DISPLAY_DC_PIN|DISPLAY_CS_PIN|DISPLAY_RST_PIN"
rg -a "203.195.202.54|api.tenclass|XIAOBINXIAOBIN" build\xiaozhi.bin
```

期望结果：

- `CONFIG_OTA_URL` 指向 `http://203.195.202.54:8766/api/ota/check`
- `config.h` 中屏幕为 `GC9A01`，分辨率为 240x240，SPI 接线为 SDA/MOSI=GPIO41、SCL/CLK=GPIO42、DC=GPIO21、CS=GPIO38、RES/RST=GPIO17
- 不应出现官方默认 `api.tenclass` 地址
- 默认唤醒词应为 `CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y`
- `CONFIG_USE_CUSTOM_WAKE_WORD` 不应启用，除非明确要构建自定义唤醒词版本

## 配置来源说明

这个项目里有三层配置需要区分：

- `config.json`：板型发布配置，提交到 git，供 `scripts/release.py` 读取。
- `sdkconfig`：本地生成配置，通常不提交，普通 `idf.py build` 只使用它和默认配置。
- 设备 NVS：运行时持久化配置，可能保存 Wi-Fi、`ota_url` 等值，并覆盖编译进固件的默认值。

因此，要保证刷入的是预期固件，需要同时确认源码提交、`sdkconfig` 内容，以及是否需要清除设备 NVS。
