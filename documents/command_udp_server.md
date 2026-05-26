# CommandUdpServer 协议说明

## 端口

- UDP 端口：`12121`

## 报文格式

单行 ASCII 文本，固定格式：

```
CMD <vx> <vy> <wz> <height> <pitch> <roll> <mode> <gait>
```

字段说明：
- `vx`：前进速度（m/s），写入 `UserCommand.forward_vel_scale`
- `vy`：侧向速度（m/s），写入 `UserCommand.side_vel_scale`
- `wz`：偏航角速度（rad/s），写入 `UserCommand.turnning_vel_scale`
- `height`：机身高度（m），写入 `UserCommand.body_height`
- `pitch`：机身俯仰（rad），写入 `UserCommand.body_pitch`
- `roll`：机身横滚（rad），写入 `UserCommand.body_roll`
- `mode`：状态机目标模式（int），写入 `UserCommand.target_mode`
- `gait`：目标步态（int），写入 `UserCommand.target_gait`

## 示例

```
CMD 0.3 0.0 0.4 0.35 0.0 0.0 6 0
```

## 解析规则

- 仅当首个单词为 `CMD`（大小写不敏感）且参数完整时生效
- 参数不足会被忽略并输出提示
