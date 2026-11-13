# Waveshare ESP32-S3-Touch-LCD-2.1

| 项目 | 参数 |
| --- | --- |
| MCU | ESP32-S3，240 MHz |
| Flash / PSRAM | 16 MB / 8 MB OPI |
| Display | ST7701，480×480，RGB565，16-bit RGB |
| Touch | CST820，I²C 地址 0x15，Watch Shell 启用触摸 |
| IMU | QMI8658，I²C 地址 0x6B（备选 0x6A） |
| RTC | PCF85063，I²C 地址 0x51 |
| IO expander | TCA9554，I²C 地址 0x20 |
| USB | ESP USB Serial/JTAG，VID 0x303A / PID 0x1001 |

## 关键引脚

| 功能 | 引脚 |
| --- | --- |
| I²C SDA / SCL | GPIO15 / GPIO7 |
| Display configuration MOSI / CLK | GPIO1 / GPIO2 |
| Backlight PWM | GPIO6 |
| RGB HSYNC / VSYNC / DE / PCLK | GPIO38 / GPIO39 / GPIO40 / GPIO41 |
| RGB data 0–15 | 5, 45, 48, 47, 21, 14, 13, 12, 11, 10, 9, 46, 3, 8, 18, 17 |
| Display reset / CS | TCA9554 EXIO1 / EXIO3 |
| Touch interrupt / reset | GPIO16 / TCA9554 EXIO2 |
| Battery voltage ADC | GPIO4 |
| Buzzer control | TCA9554 EXIO8 |

## 构建约定

完整 FQBN 与烧录参数见 `board.json`。使用 Arduino-ESP32 3.0.7、OPI PSRAM、16 MB Flash、`app3M_fat9M_16MB` 分区。

ROM boot header 使用 **DIO / 80 MHz**。即使 Arduino 内部配置为 QIO，也不要在烧录时把 bootloader 头强行改成 QIO；实机遇到过由此引起的 ROM 重启。`scripts/flash.py` 使用已验证的 DIO 参数。

## 官方驱动

来源：[微雪官方示例 ZIP](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1/ESP32-S3-Touch-LCD-2.1-Demo.zip)，其 SHA-256 固定在 `board.json`。

`scripts/prepare_board.py` 提取 `Display_ST7701.{h,cpp}` 和 `TCA9554PWR.{h,cpp}`，移除 LVGL / 触摸初始化依赖，给 RGB panel 初始化添加错误检查。生成文件被 Git 忽略；如果本地已修改这些生成文件，脚本会停止，避免覆盖。

官方 ZIP 没有项目级许可文件，因此该仓库不直接再分发这四个文件，也不为其附加新的许可。官方资料：[产品 Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.1)。
