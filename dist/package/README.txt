Windows 运行说明

1. 保持本目录内文件在同一文件夹：
   - joystick_sender.exe
   - SDL2.dll
   - udp_receiver.exe（本地接收测试工具）
   - keyboard_sender.exe（键盘发送工具）
   - config.txt（可选）

2. 双击或在命令行运行：
   joystick_sender.exe config.txt

   本地接收测试（127.0.0.1）：
   udp_receiver.exe 12121

   键盘发送测试（默认 127.0.0.1:12121）：
   keyboard_sender.exe

   键盘发送读取 config.txt（udp_host/udp_port）：
   keyboard_sender.exe

3. 调试输出：
   打开 config.txt，设置：
   debug=1
   print_every=50
   默认日志文件：joystick_sender.log
