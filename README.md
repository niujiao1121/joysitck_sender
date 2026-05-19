## 说明

本项目提供一个在 Windows 11 上运行的手柄监听程序，将手柄输入映射为 `CMD` 指令，通过 UDP 持续发送。

当前实现基于 SDL2，可在 Ubuntu 24.04 上交叉编译 Windows 可执行文件（目标机无需 Python）。
此外也提供 Linux 版本，可直接在 Ubuntu 24.04 上编译运行。

版本号与变更记录见 `VERSION.md`。

### 依赖

Windows 交叉编译依赖：

- Ubuntu 24.04
- 交叉编译工具链：`mingw-w64`
- SDL2 Windows 开发包

安装交叉编译工具链：

```bash
sudo apt-get update
sudo apt-get install -y mingw-w64
```

下载 SDL2（Windows 版）并解压：

1. 访问 https://www.libsdl.org 下载 `SDL2-devel-2.x.x-mingw.tar.gz`
2. 解压到本项目的 `deps/` 下，例如：
   `deps/SDL2-2.30.2/`

Linux 本机构建依赖：

- Ubuntu 24.04
- `g++`
- `pkg-config`
- `libsdl2-dev`

安装 Linux 构建依赖：

```bash
sudo apt-get update
sudo apt-get install -y g++ pkg-config libsdl2-dev
```

### 构建 Windows 可执行文件

```bash
export SDL2_DIR="$PWD/deps/SDL2-2.30.2/x86_64-w64-mingw32"
./build_windows.sh
```

构建完成后可在 `dist/` 目录找到 `joystick_sender.exe`。

### 构建 Linux 可执行文件

请先确保已安装 `g++`、`pkg-config` 和 `libsdl2-dev`，然后执行：

```bash
./build_linux.sh
```

构建完成后会生成：

```bash
dist/linux/joystick_sender_linux
```

运行示例：

```bash
./dist/linux/joystick_sender_linux config.txt
```

Linux 版本同样基于 SDL2，并额外处理了手柄热插拔和当前实例追踪，避免切换手柄时把旧设备状态混到新设备上。

### Windows 运行

构建完成后，直接拷贝 `dist/package/` 整个目录到 Windows 机器即可使用。
该目录内已经包含运行所需的全部文件：

- `joystick_sender.exe`
- `SDL2.dll`
- `libgcc_s_seh-1.dll`
- `libstdc++-6.dll`
- `libwinpthread-1.dll`
- `udp_receiver.exe`（本地接收测试工具）
- `keyboard_sender.exe`（键盘发送工具）
- `retroid_sender.exe`（Retroid 协议发送工具）
- `config.txt`（可选配置）
- `README.txt`（运行说明）

运行示例：

```powershell
.\joystick_sender.exe config.txt
```

本地接收测试（在 Windows 上验证 127.0.0.1）：

```powershell
.\udp_receiver.exe 12121
```

键盘发送测试（默认 127.0.0.1:12121）：

```powershell
.\keyboard_sender.exe
```

键盘发送会读取同目录 `config.txt` 中的 `udp_host`/`udp_port`，命令行参数会覆盖配置。

Retroid 协议发送：

```powershell
.\retroid_sender.exe
```

### 在 Ubuntu 上验证 UDP 接收

方式一（推荐，简单）：

```bash
nc -ul 12121
```

方式二（抓包查看）：

```bash
sudo tcpdump -n -vv -i any udp port 12121
```

### 配置文件

配置文件为 `key=value` 格式，示例见 `config.txt`。未提供配置文件时使用默认值。
可设置 `debug=1` 开启调试输出，`print_every` 控制每隔 N 帧打印一次发送内容。
`log_path` 可指定日志文件路径（默认 `joystick_sender.log`），适合双击运行时排查问题。
`raw_dump=1` 会输出所有原始轴值，用于判断 SDL 映射是否生效。
`invert_left_horizontal_axis=1` 会翻转左摇杆水平轴方向，影响 `vy`。

### 默认按键映射

基于 SDL2 的 GameController 映射，自动适配常见手柄，不需要手工调整。
默认 `mode=-1`，表示不发送外部 motion command。

- 左摇杆 X/Y -> `vx`/`vy`
- 右摇杆 X -> `wz`
- 十字键上下 -> `height` 增减
- 十字键左右 -> `roll` 增减
- LB/RB -> `pitch` 增减
- A -> `mode=1`（StandingUp/站立）
- B -> `mode=18`（LieDown/趴下）
- LB+RB -> `mode=6`（RLControlMode）
- LT+RT -> `mode=2`（JointDamping/Damping）
- X/Y -> `gait` 增减

### UDP 报文

参见 `documents/command_udp_server.md`，实际发送格式：

```
CMD <vx> <vy> <wz> <height> <pitch> <roll> <mode> <gait>
```

### 配置项说明（config.txt）

- `udp_host`/`udp_port`：UDP 目标地址
- `send_hz`：发送频率
- `axis_deadzone`：摇杆死区
- `invert_left_horizontal_axis`：是否翻转左摇杆水平轴方向（`0`/`1`）
- `vx_max`/`vy_max`/`wz_max`：速度缩放
- `height_*`/`pitch_*`/`roll_*`：机身姿态范围与步进
- `mode_*`/`gait_*`：允许范围
