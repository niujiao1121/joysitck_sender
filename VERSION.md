# 版本说明

## 版本号

当前版本：`0.2.1`

版本号规则：
- 采用 `主版本.次版本.修订号`（如 `1.2.3`）
- 修订号：小修复、日志/配置调整、文档更新
- 次版本：功能新增（如新增发送目标、新协议）
- 主版本：协议或行为重大变更

## 变更记录

### 0.2.1
- 调整 `joystick_sender` 的 `mode` 输出：默认 `-1`，A=站立，B=趴下，LB+RB=RL，LT+RT=Damping
- Windows 与 Linux 手柄发送程序版本号对齐

### 0.2.0
- 新增 Linux 版本手柄发送程序 `joystick_sender_linux`
- 新增 `build_linux.sh`，可在 Ubuntu 24.04 直接构建 Linux 可执行文件
- Linux 版本补充手柄热插拔与实例 ID 跟踪，降低切换手柄时错轴风险

### 0.1.0
- 新增 Retroid 协议发送程序 `retroid_sender.exe`
- 键盘发送程序与手柄程序对齐日志/配置能力
- 控制台输出使用 Unicode，解决中文乱码问题
