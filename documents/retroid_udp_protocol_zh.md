# Retroid Gamepad UDP 协议（完整说明）

本文档描述 Lite3_MotionSDK 中 **RetroidGamepad** 的 UDP 数据协议，完全对应代码实现：
- `gamepad/include/gamepad_keys.h`
- `gamepad/src/retroid_gamepad.cpp`
- `gamepad/src/gamepad.cpp`

---

## 1. 传输层参数
- **协议**：UDP  
- **默认端口**：`12121`（`kDefaultPort`）  
- **接收端**：Lite3_MotionSDK 内部 `UdpReceiver`

---

## 2. 数据包总结构（42 字节，紧凑无对齐）

结构体：`RetroidGamepadData`  

> 使用 `#pragma pack(1)`，无对齐填充。

### 字节布局

| 字段 | 类型 | 长度(字节) | 说明 |
|---|---|---:|---|
| stx | uint8_t[2] | 2 | 固定头 `0x55 0x66` |
| ctrl | uint8_t | 1 | 控制字节 |
| data_len | uint16_t | 2 | data 区字节数 |
| seq | uint16_t | 2 | 序号 |
| id | uint8_t | 1 | 设备类型（Retroid=1） |
| crc16 | uint16_t | 2 | data 区校验 |
| data | uint8_t[32] | 32 | 16通道 * 2字节 |

**总长度：42 字节**

---

## 3. 校验规则（严格）

解析时必须满足：

### 1) 固定头
```
stx[0] == 0x55
stx[1] == 0x66
```

### 2) 设备类型
```
id == GamepadType::kRetroid (数值 1)
```

### 3) CRC16（其实是字节求和）
在 `Gamepad::CalculateCrc16()` 中定义：
```
crc16 = sum(data[0..data_len-1])
```
即 **把 data_len 个字节逐个相加** 得到 uint16。

---

## 4. data 区内部结构（Retroid）

`data` 被解释为：

```
uint16_t buttons[10];
int16_t  left_axis_x;
int16_t  left_axis_y;
int16_t  right_axis_x;
int16_t  right_axis_y;
uint16_t axis_buttons[2];
```

### 轴值范围
```
left_axis_x/y, right_axis_x/y ∈ [-1000, +1000]
```

最终归一化：
```
axis_norm = axis / 1000.0
```

---

## 5. 按键 bit 映射（最关键）

在 `retroid_gamepad.cpp` 内，先把 `data.data` 当作 16 个 int16 通道：

```
int16_t ch[16];
memcpy(ch, data.data, sizeof(ch));
for i in 0..15:
  value_bit[i] = ch[i] (非零为1)
keys.value = value_bit.to_ulong()
```

### bit 与按键对应关系（从低位到高位）

| bit | 字段名 | 说明 |
|---:|---|---|
| 0 | R1 | 右肩键 |
| 1 | L1 | 左肩键 |
| 2 | start | Start |
| 3 | select | Select |
| 4 | R2 | 右扳机 |
| 5 | L2 | 左扳机 |
| 6 | A | A |
| 7 | B | B |
| 8 | X | X |
| 9 | Y | Y |
| 10 | left | D-pad 左 |
| 11 | right | D-pad 右 |
| 12 | up | D-pad 上 |
| 13 | down | D-pad 下 |
| 14 | left_axis_button | 左摇杆按下 |
| 15 | right_axis_button | 右摇杆按下 |

> 说明：这里的 bit 值来自 **16 个 int16 通道是否非零**，并非直接读取 `buttons[10]` 数组。

---

## 6. D-Pad 方向键的额外逻辑

代码额外根据 **左摇杆轴值** 再设置方向键（会覆盖原 D-Pad 状态）：

```
left  = (left_axis_x == -1000)
right = (left_axis_x ==  1000)
up    = (left_axis_y ==  1000)
down  = (left_axis_y == -1000)
```

---

## 7. 解析伪代码（与实现一致）

```
if stx != 0x55 0x66: reject
if id != 1: reject
if crc16 != sum(data[0..data_len-1]): reject

ch = reinterpret data as int16[16]
for i in 0..15:
  bit[i] = (ch[i] != 0)
keys.value = bitset_to_uint16(bit)

keys.left_axis_x  = left_axis_x  / 1000.0
keys.left_axis_y  = left_axis_y  / 1000.0
keys.right_axis_x = right_axis_x / 1000.0
keys.right_axis_y = right_axis_y / 1000.0

keys.left  = (left_axis_x == -1000)
keys.right = (left_axis_x ==  1000)
keys.up    = (left_axis_y ==  1000)
keys.down  = (left_axis_y == -1000)
```

---

## 8. 注意事项
- `buttons[10]` 在当前解析中 **未直接使用**。
- `axis_buttons[2]` 在当前解析中 **未直接使用**。
- D-Pad 最终值可能被左摇杆逻辑覆盖。
- `crc16` 实际是简单求和，非标准 CRC16。

