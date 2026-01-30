# 项目要求与说明

## 目标

编写一个在 Windows 11 上运行的手柄指令接收程序，并将指令以 UDP 协议发送。

## 环境

- 编译环境：Ubuntu 24.04
- Conda 环境名：tool_joystick
- Python 版本：3.12.0
- 运行环境：Windows 11

## UDP 协议

详见 `documents/command_udp_server.md`。

## 要求

1. 若使用 Python 编写，请使用除 pygame 以外的手柄库（pygame 支持较差）。
2. 对不同手柄尽量做到即插即用，无需手动调整按键/摇杆映射。
3. 编译后的程序尽量减少对 Windows 11 环境的依赖，便于分发。，win11上没有make, python, conda等东西
4. 有不清晰的地方请直接提问，不要含糊处理。
5. 代码需提供充分注释，便于后续维护。
6. 提供详细使用说明，方便用户上手。
7. 代码注释使用中文。
