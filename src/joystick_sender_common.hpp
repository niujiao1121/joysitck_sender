#pragma once

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace joystick_sender {

inline constexpr const char* kVersion = "0.2.4";
inline constexpr Sint16 kActiveAxisThreshold = 12000;
inline constexpr float kTriggerPressedThreshold = 0.5f;
inline constexpr int kModeStandingUp = 1;
inline constexpr int kModeJointDamping = 2;
inline constexpr int kModeRLControl = 6;
inline constexpr int kModeLieDown = 18;

struct Config {
  std::string host = "127.0.0.1";
  int port = 12121;
  int send_hz = 50;
  bool debug = false;
  int print_every = 50;
  std::string log_path = "joystick_sender.log";
  bool raw_dump = false;
  float axis_deadzone = 0.05f;
  bool invert_left_horizontal_axis = true;
  float vx_max = 0.5f;
  float vy_max = 0.5f;
  float wz_max = 1.0f;
  float height_min = 0.2f;
  float height_max = 0.5f;
  float height_step = 0.01f;
  float pitch_min = -0.3f;
  float pitch_max = 0.3f;
  float pitch_step = 0.02f;
  float roll_min = -0.3f;
  float roll_max = 0.3f;
  float roll_step = 0.02f;
  int mode_min = 0;
  int mode_max = 10;
  int gait_min = 0;
  int gait_max = 5;
};

struct State {
  float height = 0.35f;
  float pitch = 0.0f;
  float roll = 0.0f;
  int mode = -1;
  int gait = 0;
  std::map<SDL_GameControllerButton, bool> last_buttons;
};

struct ControllerContext {
  SDL_GameController* controller = nullptr;
  SDL_Joystick* joystick = nullptr;
  SDL_JoystickID instance_id = -1;
  std::string name;
};

struct UiPanel {
  bool enabled = false;
  bool first_render = true;
  uint32_t platform_mode = 0;
  uint32_t last_render_ms = 0;
  std::string status = "starting";
  std::string controller = "none";
  std::string axes = "lx=0.000 ly=0.000 rx=0.000 ry=0.000 lt=0.000 rt=0.000";
  std::string raw_axes = "原始轴: n/a";
  std::string raw_buttons = "原始按键: n/a";
  std::string command = "CMD n/a";
  std::string event = "等待输入";
  int send_count = 0;
  int send_fail_count = 0;
};

struct CommandFrame {
  float lx = 0.0f;
  float ly = 0.0f;
  float rx = 0.0f;
  float ry = 0.0f;
  float lt = 0.0f;
  float rt = 0.0f;
  float vx = 0.0f;
  float vy = 0.0f;
  float wz = 0.0f;
  std::string command = "CMD n/a";
  std::string axes = "lx=0.000 ly=0.000 rx=0.000 ry=0.000 lt=0.000 rt=0.000";
};

inline bool FileExists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

inline float Clamp(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

inline float ApplyDeadzone(float v, float deadzone) {
  return (std::abs(v) < deadzone) ? 0.0f : v;
}

inline float NormalizeAxis(Sint16 value) {
  if (value < 0) {
    return static_cast<float>(value) / 32768.0f;
  }
  return static_cast<float>(value) / 32767.0f;
}

inline float NormalizeTrigger(Sint16 value) {
  return static_cast<float>(value) / 32767.0f;
}

inline bool ParseConfigLine(const std::string& line, std::string* key, std::string* value) {
  auto pos = line.find('=');
  if (pos == std::string::npos) {
    return false;
  }
  *key = line.substr(0, pos);
  *value = line.substr(pos + 1);
  return true;
}

inline void Trim(std::string* s) {
  const char* whitespace = " \t\r\n";
  auto start = s->find_first_not_of(whitespace);
  if (start == std::string::npos) {
    s->clear();
    return;
  }
  auto end = s->find_last_not_of(whitespace);
  *s = s->substr(start, end - start + 1);

  const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
  if (s->size() >= 3 &&
      static_cast<unsigned char>((*s)[0]) == bom[0] &&
      static_cast<unsigned char>((*s)[1]) == bom[1] &&
      static_cast<unsigned char>((*s)[2]) == bom[2]) {
    *s = s->substr(3);
  }
}

inline bool ParseBool(const std::string& value) {
  std::string v = value;
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return v == "1" || v == "true" || v == "yes" || v == "on";
}

inline void LoadConfig(const std::string& path, Config* config, bool echo_items = false) {
  std::ifstream file(path);
  if (!file.is_open()) {
    std::fprintf(stderr, "配置文件未找到，使用默认配置: %s\n", path.c_str());
    return;
  }
  std::fprintf(stderr, "读取配置文件: %s\n", path.c_str());
  std::string line;
  while (std::getline(file, line)) {
    Trim(&line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::string key;
    std::string value;
    if (!ParseConfigLine(line, &key, &value)) {
      continue;
    }
    Trim(&key);
    Trim(&value);
    if (echo_items) {
      std::fprintf(stderr, "配置项: %s=%s\n", key.c_str(), value.c_str());
    }
    if (key == "udp_host") {
      config->host = value;
    } else if (key == "udp_port") {
      config->port = std::stoi(value);
    } else if (key == "send_hz") {
      config->send_hz = std::stoi(value);
    } else if (key == "debug") {
      config->debug = ParseBool(value);
    } else if (key == "print_every") {
      config->print_every = std::max(1, std::stoi(value));
    } else if (key == "log_path") {
      config->log_path = value;
    } else if (key == "raw_dump") {
      config->raw_dump = ParseBool(value);
    } else if (key == "axis_deadzone") {
      config->axis_deadzone = std::stof(value);
    } else if (key == "invert_left_horizontal_axis" || key == "invert_horizontal_axis") {
      config->invert_left_horizontal_axis = ParseBool(value);
    } else if (key == "vx_max") {
      config->vx_max = std::stof(value);
    } else if (key == "vy_max") {
      config->vy_max = std::stof(value);
    } else if (key == "wz_max") {
      config->wz_max = std::stof(value);
    } else if (key == "height_min") {
      config->height_min = std::stof(value);
    } else if (key == "height_max") {
      config->height_max = std::stof(value);
    } else if (key == "height_step") {
      config->height_step = std::stof(value);
    } else if (key == "pitch_min") {
      config->pitch_min = std::stof(value);
    } else if (key == "pitch_max") {
      config->pitch_max = std::stof(value);
    } else if (key == "pitch_step") {
      config->pitch_step = std::stof(value);
    } else if (key == "roll_min") {
      config->roll_min = std::stof(value);
    } else if (key == "roll_max") {
      config->roll_max = std::stof(value);
    } else if (key == "roll_step") {
      config->roll_step = std::stof(value);
    } else if (key == "mode_min") {
      config->mode_min = std::stoi(value);
    } else if (key == "mode_max") {
      config->mode_max = std::stoi(value);
    } else if (key == "gait_min") {
      config->gait_min = std::stoi(value);
    } else if (key == "gait_max") {
      config->gait_max = std::stoi(value);
    }
  }
}

inline void ResetSnapshots(State* state, std::vector<int>* last_axes,
                           std::vector<int>* last_buttons, int* last_hat) {
  state->last_buttons.clear();
  last_axes->clear();
  last_buttons->clear();
  *last_hat = 0;
}

inline void CloseController(ControllerContext* ctx) {
  if (ctx->controller) {
    SDL_GameControllerClose(ctx->controller);
  }
  ctx->controller = nullptr;
  ctx->joystick = nullptr;
  ctx->instance_id = -1;
  ctx->name.clear();
}

template <typename Logger>
bool OpenControllerByIndex(int device_index, ControllerContext* ctx, Logger log) {
  if (!SDL_IsGameController(device_index)) {
    return false;
  }

  SDL_GameController* controller = SDL_GameControllerOpen(device_index);
  if (!controller) {
    log("打开手柄[%d]失败: %s\n", device_index, SDL_GetError());
    return false;
  }

  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SDL_JoystickID instance_id = SDL_JoystickInstanceID(joystick);
  const char* name = SDL_GameControllerName(controller);

  CloseController(ctx);
  ctx->controller = controller;
  ctx->joystick = joystick;
  ctx->instance_id = instance_id;
  ctx->name = name ? name : "unknown";

  log("已打开手柄: index=%d instance_id=%d name=%s\n",
      device_index, static_cast<int>(instance_id), ctx->name.c_str());
  log("手柄轴数量: %d, 按键数量: %d, Hat 数量: %d\n",
      SDL_JoystickNumAxes(joystick),
      SDL_JoystickNumButtons(joystick),
      SDL_JoystickNumHats(joystick));
  return true;
}

inline ControllerContext* FindControllerByInstanceId(
    std::vector<ControllerContext>* controllers, SDL_JoystickID instance_id) {
  for (auto& controller : *controllers) {
    if (controller.instance_id == instance_id) {
      return &controller;
    }
  }
  return nullptr;
}

template <typename Logger>
bool OpenAllControllers(std::vector<ControllerContext>* controllers, Logger log) {
  bool opened = false;
  const int joystick_count = SDL_NumJoysticks();
  log("当前检测到手柄数量: %d\n", joystick_count);
  for (int i = 0; i < joystick_count; ++i) {
    const char* name = SDL_JoystickNameForIndex(i);
    log("发现设备[%d]: %s game_controller=%d\n",
        i, name ? name : "unknown", SDL_IsGameController(i) ? 1 : 0);
    ControllerContext ctx;
    if (OpenControllerByIndex(i, &ctx, log)) {
      controllers->push_back(ctx);
      opened = true;
    }
  }
  return opened;
}

template <typename Logger>
bool AddControllerByIndex(int device_index, std::vector<ControllerContext>* controllers,
                          Logger log) {
  ControllerContext ctx;
  if (!OpenControllerByIndex(device_index, &ctx, log)) {
    return false;
  }
  if (FindControllerByInstanceId(controllers, ctx.instance_id)) {
    CloseController(&ctx);
    return false;
  }
  controllers->push_back(ctx);
  return true;
}

inline void RemoveControllerByInstanceId(std::vector<ControllerContext>* controllers,
                                         SDL_JoystickID instance_id) {
  for (auto it = controllers->begin(); it != controllers->end(); ++it) {
    if (it->instance_id == instance_id) {
      CloseController(&(*it));
      controllers->erase(it);
      return;
    }
  }
}

template <typename Logger>
ControllerContext* ResolveActiveController(
    std::vector<ControllerContext>* controllers, SDL_JoystickID* active_instance_id,
    Logger log) {
  ControllerContext* active = FindControllerByInstanceId(controllers, *active_instance_id);
  if (active) {
    return active;
  }
  if (controllers->empty()) {
    *active_instance_id = -1;
    return nullptr;
  }
  *active_instance_id = controllers->front().instance_id;
  log("活动手柄切换为: instance_id=%d name=%s\n",
      static_cast<int>(*active_instance_id), controllers->front().name.c_str());
  return &controllers->front();
}

template <typename Logger>
void ActivateController(SDL_JoystickID instance_id,
                        std::vector<ControllerContext>* controllers,
                        SDL_JoystickID* active_instance_id,
                        Logger log) {
  if (*active_instance_id == instance_id) {
    return;
  }
  ControllerContext* controller = FindControllerByInstanceId(controllers, instance_id);
  if (!controller) {
    return;
  }
  *active_instance_id = instance_id;
  log("活动手柄切换为: instance_id=%d name=%s\n",
      static_cast<int>(instance_id), controller->name.c_str());
}

inline void UpdateRawState(SDL_Joystick* joystick, UiPanel* ui,
                           std::vector<int>* last_axes,
                           std::vector<int>* last_buttons, int* last_hat) {
  if (!joystick) {
    ui->raw_axes = "原始轴: n/a";
    ui->raw_buttons = "原始按键: n/a";
    return;
  }

  int axes = SDL_JoystickNumAxes(joystick);
  int buttons = SDL_JoystickNumButtons(joystick);
  int hats = SDL_JoystickNumHats(joystick);

  if (static_cast<int>(last_axes->size()) != axes) {
    last_axes->assign(axes, 0);
  }
  if (static_cast<int>(last_buttons->size()) != buttons) {
    last_buttons->assign(buttons, 0);
  }

  std::string axis_line = "原始轴:";
  for (int i = 0; i < axes; ++i) {
    int raw = SDL_JoystickGetAxis(joystick, i);
    (*last_axes)[i] = raw;
    char buf[32];
    std::snprintf(buf, sizeof(buf), " a%d=%d", i, raw);
    axis_line += buf;
  }

  std::string button_line = "原始按键:";
  for (int i = 0; i < buttons; ++i) {
    int pressed = SDL_JoystickGetButton(joystick, i);
    (*last_buttons)[i] = pressed;
    char buf[24];
    std::snprintf(buf, sizeof(buf), " b%d=%d", i, pressed);
    button_line += buf;
  }

  int hat = (hats > 0) ? SDL_JoystickGetHat(joystick, 0) : 0;
  *last_hat = hat;
  char hat_buf[16];
  std::snprintf(hat_buf, sizeof(hat_buf), " hat=%d", hat);
  ui->raw_axes = axis_line;
  ui->raw_buttons = button_line + hat_buf;
}

template <typename Logger>
void LogRawState(SDL_Joystick* joystick, std::vector<int>* last_axes,
                 std::vector<int>* last_buttons, int* last_hat, Logger log) {
  UiPanel ui;
  UpdateRawState(joystick, &ui, last_axes, last_buttons, last_hat);
  if (joystick) {
    log("%s\n", ui.raw_axes.c_str());
    log("%s\n", ui.raw_buttons.c_str());
  }
}

inline void HandleButtonEdge(State* state, const Config& config,
                             SDL_GameControllerButton button, bool pressed) {
  bool last = state->last_buttons[button];
  state->last_buttons[button] = pressed;
  if (!pressed || last) {
    return;
  }

  if (button == SDL_CONTROLLER_BUTTON_A) {
    state->mode = kModeStandingUp;
  } else if (button == SDL_CONTROLLER_BUTTON_B) {
    state->mode = kModeLieDown;
  } else if (button == SDL_CONTROLLER_BUTTON_X) {
    state->gait = std::min(config.gait_max, state->gait + 1);
  } else if (button == SDL_CONTROLLER_BUTTON_Y) {
    state->gait = std::max(config.gait_min, state->gait - 1);
  } else if (button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
    state->pitch = Clamp(state->pitch - config.pitch_step, config.pitch_min, config.pitch_max);
  } else if (button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
    state->pitch = Clamp(state->pitch + config.pitch_step, config.pitch_min, config.pitch_max);
  }
}

inline void HandleDpad(State* state, const Config& config,
                       SDL_GameControllerButton button, bool pressed) {
  if (!pressed) {
    return;
  }
  if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
    state->height = Clamp(state->height + config.height_step, config.height_min, config.height_max);
  } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
    state->height = Clamp(state->height - config.height_step, config.height_min, config.height_max);
  } else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
    state->roll = Clamp(state->roll + config.roll_step, config.roll_min, config.roll_max);
  } else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
    state->roll = Clamp(state->roll - config.roll_step, config.roll_min, config.roll_max);
  }
}

inline CommandFrame BuildCommandFrame(SDL_GameController* controller,
                                      const Config& config, State* state) {
  CommandFrame frame;
  if (!controller) {
    return frame;
  }

  frame.lx = NormalizeAxis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
  frame.ly = NormalizeAxis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
  frame.rx = NormalizeAxis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX));
  frame.ry = NormalizeAxis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
  frame.lt = NormalizeTrigger(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
  frame.rt = NormalizeTrigger(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

  frame.lx = ApplyDeadzone(frame.lx, config.axis_deadzone);
  frame.ly = ApplyDeadzone(frame.ly, config.axis_deadzone);
  frame.rx = ApplyDeadzone(frame.rx, config.axis_deadzone);
  frame.ry = ApplyDeadzone(frame.ry, config.axis_deadzone);

  if (config.invert_left_horizontal_axis) {
    frame.lx = -frame.lx;
  }

  frame.vx = -frame.ly * config.vx_max;
  frame.vy = frame.lx * config.vy_max;
  frame.wz = -frame.rx * config.wz_max;

  bool lb = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
  bool rb = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  if (lb && rb) {
    state->mode = kModeRLControl;
  } else if (frame.lt >= kTriggerPressedThreshold && frame.rt >= kTriggerPressedThreshold) {
    state->mode = kModeJointDamping;
  }

  char command_buf[256];
  std::snprintf(command_buf, sizeof(command_buf),
                "CMD %.3f %.3f %.3f %.3f %.3f %.3f %d %d",
                frame.vx, frame.vy, frame.wz,
                state->height, state->pitch, state->roll, state->mode, state->gait);
  frame.command = command_buf;

  char axes_buf[160];
  std::snprintf(axes_buf, sizeof(axes_buf),
                "lx=%.3f ly=%.3f rx=%.3f ry=%.3f lt=%.3f rt=%.3f",
                frame.lx, frame.ly, frame.rx, frame.ry, frame.lt, frame.rt);
  frame.axes = axes_buf;

  return frame;
}

inline std::string FormatController(const ControllerContext* controller) {
  if (!controller) {
    return "none";
  }
  char buf[512];
  std::snprintf(buf, sizeof(buf), "instance_id=%d name=%s",
                static_cast<int>(controller->instance_id), controller->name.c_str());
  return buf;
}

}  // namespace joystick_sender
