# Gravity Clock

适配 Waveshare ESP32-S3-Touch-LCD-2.1 的重力时钟。

- 大号 24 小时时间、星期、日期、秒数与外圈秒刻度。
- 当前固定显示 UTC+8，RTC 内部保存 UTC。
- 用加速度计和陀螺仪估计重力，连续旋转画面；加入平滑、角度死区和最短方向插值。
- 加速度模长超出 0.8–1.2 g 时暂停加速度校正；接近平放时通过滞回保持上一次方向。
- `HAND -1`、`OFFSET 90` 是这块板上验证过的默认映射，校准值保存在 NVS。
- USB 同步系统时间并写入 RTC，重启后读取 RTC。完全断电后的走时取决于 RTC 是否有后备供电。

## 构建

在仓库根目录执行：

```sh
.venv/bin/python scripts/build.py gravity-clock
.venv/bin/python scripts/flash.py gravity-clock --port /dev/cu.usbmodemXXXX
.venv/bin/python examples/gravity-clock/clock_control.py --sync
```

`scripts/prepare_board.py` 会准备四个来自官方示例的显示/IO 扩展驱动文件，它们不在仓库中。构建依赖 Arduino-ESP32 3.0.7、OPI PSRAM 和 16 MB Flash，不依赖 LVGL。

## 控制脚本

```sh
.venv/bin/python examples/gravity-clock/clock_control.py
.venv/bin/python examples/gravity-clock/clock_control.py --sync
.venv/bin/python examples/gravity-clock/clock_control.py --brightness 55
.venv/bin/python examples/gravity-clock/clock_control.py --watch 20
.venv/bin/python examples/gravity-clock/clock_control.py --capture build/screen.png
.venv/bin/python examples/gravity-clock/clock_control.py --hand -1 --offset 90
```

只连接一块 ESP USB Serial/JTAG 设备时自动选择；多设备时加 `--port`。`--zero` 会把当前姿态定义为屏幕原生正立方向，通常不需要调用。脚本以 DTR high / RTS low 打开串口，避免已知的 USB 控制器复位组合。

## 串口协议

115200 baud，文本指令以换行结束。

| 指令 | 行为 |
| --- | --- |
| `STATUS` | JSON 状态：时间、IMU、旋转角、帧计数、内存、错误计数 |
| `TIME <unix-seconds>` | 校准系统时间和 RTC |
| `HAND -1` / `HAND 1` | 方向映射符号，保存到 NVS |
| `OFFSET <degrees>` | 角度偏移，保存到 NVS |
| `ZERO` | 以当前竖直姿态校准偏移 |
| `BRIGHTNESS <5..100>` | 背光百分比，保存到 NVS |
| `STREAM 1` / `STREAM 0` | 开启/关闭周期性 JSON 输出 |
| `CAPTURE` | 头 `RGB565 480 480 460800`，随后为 460800 字节小端 RGB565 数据及结束标记 |

设备无时钟有效值时显示 `SYNC TIME VIA USB`。没有配置 Wi-Fi 自动校时或主机后台服务。

## 验证记录与限制

2026-11-13 的原始版本完成了实机测试：显示正常、旋转方向经用户确认、RTC 和方向参数在重启后恢复、IMU 读写错误数为零。重启采样时与主机时间相差约 1.2 秒。接近平放时进入保持模式。

仓库整理时，将个人 Mac 上的 DIN / Arial 字形替换为附带 OFL 许可的 Barlow 字形，并统一代码格式。公开版通过固件编译和本地渲染检查后，于同日刷入开发板，写入哈希校验通过。USB 校时成功，原有 `HAND -1` / `OFFSET 90` 校准保留，IMU 读写错误数为零。从设备读回的 480×480 显示缓冲区确认 Barlow 数字、日期和秒刻度正常；这项检查是设备缓冲区读取，不是屏幕的相机拍摄。

静止时主要每秒刷新，旋转时增加更新频率；原始实机单帧观测约 60–100 ms。快速运动、长时间强线性加速度和未校准的陀螺仪偏置会影响方向估计，接近平放时无法从重力确定屏幕平面内的正立方向。

字体可通过根目录的 `scripts/generate_fonts.py` 重新生成；渲染器为 RGB565 软件绘图，任意角度旋转使用最近邻采样。
