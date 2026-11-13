# Watch Shell

480×480 圆屏上的英文手表界面：重力表盘、应用网格、快捷设置、秒表、倒计时、运动传感器、硬件信息和 Wi-Fi 配网。它使用板载 CST820 触摸与 QMI8658 IMU，延续 Gravity Clock 的 Barlow 字体、覆盖率抗锯齿和双线性旋转。

![Watch Shell pages](../../docs/watch-shell.png)

预览来自与固件相同的绘图代码，数值是演示数据。

## 操作

| 位置 / 手势 | 功能 |
| --- | --- |
| 表盘上滑或点击 | 打开应用网格 |
| 表盘下滑 | 快捷设置：Wi-Fi、表盘旋转、亮度、熄屏 |
| 表盘左滑 | 运动传感器 |
| 应用内右滑 / 左上角返回 | 返回应用网格 |
| 应用网格下滑或右滑 | 返回表盘 |
| 长按约 0.7 秒 | 返回表盘 |
| 熄屏后触摸 | 唤醒，首次触摸不执行按钮动作 |

表盘随重力连续旋转；离开表盘时，应用界面固定到最近的 90° 方向，触摸坐标对应显示方向。回到表盘再进入应用可重新确定菜单方向。

## 应用与设置

- **Clock**：时间、日期、星期、秒刻度。时区为 UTC+8；RTC 存 UTC。
- **Stopwatch**：开始、暂停、继续、复位。切换页面或熄屏后继续计时。
- **Timer**：1 / 3 / 5 / 10 分钟预设，暂停、继续、复位。到时打开计时器页面并短鸣提醒。
- **Motion**：加速度、角速度和倾斜指示。数值是板卡坐标系。
- **Device**：CPU、屏幕、Flash / PSRAM、可用内存、ADC 电压、固件版本。没有电池时，Battery input 不能作为电量判断。
- **Settings**：亮度滑条、表盘自动旋转、12 / 24 小时格式、15 / 30 / 60 秒或不自动熄屏。设置保存到 NVS。
- **Wi-Fi**：开关与手机网页配网；连接互联网后自动 NTP 校时并更新 RTC。

熄屏只关闭背光并暂停绘图，不是 ESP32 深度睡眠。计时器和秒表不跨断电保存。

## Wi-Fi 配网

1. 打开 **Wi-Fi → Set up Wi-Fi**。
2. 用手机连接圆屏显示的 `RoundLab-xxxx` 热点，输入圆屏上的临时密码。
3. 在浏览器打开 `http://192.168.4.1`，选择或输入 **2.4 GHz** 网络名和密码，点击 **Connect**。
4. 成功连接后热点会自动关闭，设备使用该网络自动校时；之后开机自动重连。连接失败可重新提交配置。

Wi-Fi 凭据只保存到设备 NVS，不写入源码或串口状态。首次启动默认关闭 Wi-Fi；未联网也能使用 RTC 或 USB 校时。

## 构建与烧录

在仓库根目录执行（环境安装见根 README）：

```sh
.venv/bin/python scripts/build.py watch-shell
.venv/bin/python scripts/flash.py watch-shell --port /dev/cu.usbmodemXXXX
.venv/bin/python examples/watch-shell/watch_control.py --sync --page face
```

与 Gravity Clock 是独立固件；烧录其中一个即运行对应示例。显示驱动由构建工具从官方演示包生成，详见板卡说明。

## 调试和预览

```sh
# 状态与实际 framebuffer 截图
.venv/bin/python examples/watch-shell/watch_control.py
.venv/bin/python examples/watch-shell/watch_control.py --page apps --capture build/watch-shell/apps.png

# 在电脑上生成九个页面，不连接设备
mkdir -p build/watch-shell/preview
c++ -std=c++17 -O2 examples/watch-shell/preview.cpp -o build/watch-shell/preview-ui
build/watch-shell/preview-ui build/watch-shell/preview

# 修改字体尺寸后重新生成字体数据
.venv/bin/python scripts/generate_watch_fonts.py
```

串口每行一条命令：`STATUS`、`TIME <UTC epoch>`、`WAKE`、`PAGE <name>`、`CAPTURE`。页面名为 `face / apps / controls / settings / stopwatch / timer / sensors / info / wifi`。截图协议与 Gravity Clock 相同，返回 RGB565 小端像素。

`WatchUi.h` 负责绘制和页面状态，`WatchShell.ino` 负责设备、触摸、应用逻辑、网络与持久化。添加内置应用直接增加页面和对应绘制、点击逻辑即可；当前没有动态安装应用、手机通知同步或后台应用管理。

显示像素时钟使用 8 MHz，为 Wi-Fi 和绘图留出内存带宽。此 Arduino 构建写 Flash 时会暂停 PSRAM 访问，因此保存设置、配网凭据和初始化 Wi-Fi 后显式重新同步 RGB DMA，处理实机遇到的画面整体偏移；详见[乐鑫 RGB 屏幕漂移说明](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html#why-do-i-get-drift-overall-drift-of-the-display-when-esp32-s3-is-driving-an-rgb-lcd-screen)。CST820 选择寄存器后发送 STOP，再读取数据，与原厂触摸驱动一致。

## 验证范围

2026-11-13：固件编译、烧录校验、RTC 读时与 USB 校时成功；九个页面通过同源绘图预览检查，表盘通过设备 framebuffer 截图检查。修复显示偏移后，实机操作确认画面居中、应用点击和滑动正常，并打开 Wi-Fi 配网入口。串口状态确认触摸和 IMU 应答。

尚未完成实际家庭网络连接、NTP 自动校时、倒计时到时蜂鸣及长时间运行验证。电脑预览与 framebuffer 截图无法验证物理屏幕的扫描偏移，因此显示稳定性也依赖上述实机观察。
