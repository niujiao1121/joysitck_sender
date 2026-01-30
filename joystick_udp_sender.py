import argparse
import json
import socket
import threading
import time
from dataclasses import dataclass, field
from typing import Dict, Optional, Tuple

from inputs import get_gamepad


DEFAULT_CONFIG = {
    "udp_host": "127.0.0.1",
    "udp_port": 12121,
    "send_hz": 50,
    "axis_deadzone": 0.05,
    "vx_max": 0.5,
    "vy_max": 0.5,
    "wz_max": 1.0,
    "height_min": 0.2,
    "height_max": 0.5,
    "height_step": 0.01,
    "pitch_min": -0.3,
    "pitch_max": 0.3,
    "pitch_step": 0.02,
    "roll_min": -0.3,
    "roll_max": 0.3,
    "roll_step": 0.02,
    "mode_min": 0,
    "mode_max": 10,
    "gait_min": 0,
    "gait_max": 5,
}


AXIS_PREFERENCES = {
    "lx": ["ABS_X"],
    "ly": ["ABS_Y"],
    "rx": ["ABS_RX", "ABS_Z"],
    "ry": ["ABS_RY", "ABS_RZ"],
    "lt": ["ABS_Z"],
    "rt": ["ABS_RZ"],
}


BUTTON_MODE_UP = {"BTN_SOUTH"}  # A
BUTTON_MODE_DOWN = {"BTN_EAST"}  # B
BUTTON_GAIT_UP = {"BTN_WEST"}  # X
BUTTON_GAIT_DOWN = {"BTN_NORTH"}  # Y
BUTTON_ROLL_LEFT = {"BTN_TL"}  # LB
BUTTON_ROLL_RIGHT = {"BTN_TR"}  # RB


@dataclass
class AxisState:
    value: int = 0
    min_val: int = 0
    max_val: int = 0

    def update(self, new_value: int) -> None:
        self.value = new_value
        if self.min_val == self.max_val == 0:
            self.min_val = new_value
            self.max_val = new_value
            return
        if new_value < self.min_val:
            self.min_val = new_value
        if new_value > self.max_val:
            self.max_val = new_value

    def is_centered(self) -> bool:
        return self.min_val < 0 < self.max_val


@dataclass
class ControllerState:
    axes: Dict[str, AxisState] = field(default_factory=dict)
    buttons: Dict[str, int] = field(default_factory=dict)
    hat_x: int = 0
    hat_y: int = 0
    height: float = 0.35
    pitch: float = 0.0
    roll: float = 0.0
    mode: int = 0
    gait: int = 0
    lock: threading.Lock = field(default_factory=threading.Lock)


def load_config(path: Optional[str]) -> Dict[str, float]:
    config = DEFAULT_CONFIG.copy()
    if not path:
        return config
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    config.update(data)
    return config


def clamp(value: float, minimum: float, maximum: float) -> float:
    return max(minimum, min(maximum, value))


def normalize_axis(axis: AxisState, centered: bool) -> float:
    # 居中摇杆归一化到 [-1, 1]，扳机归一化到 [0, 1]。
    if centered:
        scale = max(abs(axis.min_val), abs(axis.max_val))
        if scale == 0:
            return 0.0
        return axis.value / scale
    if axis.max_val == axis.min_val:
        return 0.0
    return (axis.value - axis.min_val) / (axis.max_val - axis.min_val)


def apply_deadzone(value: float, deadzone: float) -> float:
    if abs(value) < deadzone:
        return 0.0
    return value


def select_axis_code(
    axes: Dict[str, AxisState], codes: list[str], require_centered: Optional[bool]
) -> Optional[str]:
    # 按优先顺序选择符合类型的轴。
    for code in codes:
        axis = axes.get(code)
        if not axis:
            continue
        if require_centered is None:
            return code
        if axis.is_centered() == require_centered:
            return code
    return None


def event_thread(state: ControllerState, config: Dict[str, float]) -> None:
    # 后台线程：读取事件，更新轴/按键，并处理一次性调节。
    last_buttons: Dict[str, int] = {}
    while True:
        events = get_gamepad()
        with state.lock:
            for event in events:
                if event.ev_type == "Absolute":
                    if event.code.startswith("ABS_HAT0X"):
                        state.hat_x = event.state
                    elif event.code.startswith("ABS_HAT0Y"):
                        state.hat_y = event.state
                    else:
                        axis = state.axes.setdefault(event.code, AxisState())
                        axis.update(int(event.state))
                elif event.ev_type == "Key":
                    state.buttons[event.code] = event.state

            # 仅在按键从松开到按下时触发一次动作。
            for code, value in state.buttons.items():
                last_value = last_buttons.get(code, 0)
                if value == 1 and last_value == 0:
                    if code in BUTTON_MODE_UP:
                        state.mode = min(
                            int(config["mode_max"]), state.mode + 1
                        )
                    elif code in BUTTON_MODE_DOWN:
                        state.mode = max(
                            int(config["mode_min"]), state.mode - 1
                        )
                    elif code in BUTTON_GAIT_UP:
                        state.gait = min(
                            int(config["gait_max"]), state.gait + 1
                        )
                    elif code in BUTTON_GAIT_DOWN:
                        state.gait = max(
                            int(config["gait_min"]), state.gait - 1
                        )
                    elif code in BUTTON_ROLL_LEFT:
                        state.roll = clamp(
                            state.roll - config["roll_step"],
                            config["roll_min"],
                            config["roll_max"],
                        )
                    elif code in BUTTON_ROLL_RIGHT:
                        state.roll = clamp(
                            state.roll + config["roll_step"],
                            config["roll_min"],
                            config["roll_max"],
                        )
                last_buttons[code] = value

            # 为避免重复累加，十字键每次按下只调整一次。
            if state.hat_y != 0:
                step = config["height_step"] * (1 if state.hat_y > 0 else -1)
                state.height = clamp(
                    state.height + step,
                    config["height_min"],
                    config["height_max"],
                )
                state.hat_y = 0

            if state.hat_x != 0:
                step = config["pitch_step"] * (1 if state.hat_x > 0 else -1)
                state.pitch = clamp(
                    state.pitch + step,
                    config["pitch_min"],
                    config["pitch_max"],
                )
                state.hat_x = 0


def build_packet(
    state: ControllerState, config: Dict[str, float]
) -> Tuple[str, Dict[str, float]]:
    with state.lock:
        axes = state.axes.copy()
        height = state.height
        pitch = state.pitch
        roll = state.roll
        mode = state.mode
        gait = state.gait

    # 解析物理轴对应的逻辑摇杆。
    lx_code = select_axis_code(axes, AXIS_PREFERENCES["lx"], True)
    ly_code = select_axis_code(axes, AXIS_PREFERENCES["ly"], True)
    rx_code = select_axis_code(axes, AXIS_PREFERENCES["rx"], True)
    ry_code = select_axis_code(axes, AXIS_PREFERENCES["ry"], True)

    lx = normalize_axis(axes[lx_code], True) if lx_code else 0.0
    ly = normalize_axis(axes[ly_code], True) if ly_code else 0.0
    rx = normalize_axis(axes[rx_code], True) if rx_code else 0.0
    ry = normalize_axis(axes[ry_code], True) if ry_code else 0.0

    lx = apply_deadzone(lx, config["axis_deadzone"])
    ly = apply_deadzone(ly, config["axis_deadzone"])
    rx = apply_deadzone(rx, config["axis_deadzone"])
    ry = apply_deadzone(ry, config["axis_deadzone"])

    # 多数手柄前进方向对应 Y 轴负值。
    vx = -ly * config["vx_max"]
    vy = lx * config["vy_max"]
    wz = rx * config["wz_max"]

    packet = f"CMD {vx:.3f} {vy:.3f} {wz:.3f} {height:.3f} {pitch:.3f} {roll:.3f} {mode} {gait}"
    debug = {
        "lx": lx,
        "ly": ly,
        "rx": rx,
        "ry": ry,
        "height": height,
        "pitch": pitch,
        "roll": roll,
        "mode": mode,
        "gait": gait,
    }
    return packet, debug


def main() -> None:
    parser = argparse.ArgumentParser(description="Joystick UDP sender")
    parser.add_argument("--config", help="Path to config.json")
    parser.add_argument("--host", help="UDP target host override")
    parser.add_argument("--port", type=int, help="UDP target port override")
    parser.add_argument("--print-every", type=int, default=0, help="Print every N packets")
    args = parser.parse_args()

    config = load_config(args.config)
    if args.host:
        config["udp_host"] = args.host
    if args.port:
        config["udp_port"] = args.port

    state = ControllerState(
        height=DEFAULT_CONFIG["height_min"]
        + (DEFAULT_CONFIG["height_max"] - DEFAULT_CONFIG["height_min"]) / 2.0,
        mode=int(config["mode_min"]),
        gait=int(config["gait_min"]),
    )

    thread = threading.Thread(target=event_thread, args=(state, config), daemon=True)
    thread.start()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    period = 1.0 / config["send_hz"]
    next_send = time.monotonic()
    count = 0

    while True:
        now = time.monotonic()
        if now < next_send:
            time.sleep(min(0.002, next_send - now))
            continue

        packet, debug = build_packet(state, config)
        sock.sendto(packet.encode("ascii"), (config["udp_host"], int(config["udp_port"])))
        count += 1
        if args.print_every and count % args.print_every == 0:
            print(packet, debug)

        next_send += period


if __name__ == "__main__":
    main()
