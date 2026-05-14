#include <SDL2/SDL.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static bool FileExists(const std::string& path) {
  std::ifstream file(path);
  return file.good();
}

static const char* kVersion = "0.2.1";

struct Config {
  std::string host = "127.0.0.1";
  int port = 12121;
  int send_hz = 50;
  bool debug = false;
  int print_every = 50;
  std::string log_path = "joystick_sender_linux.log";
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

  // 记录按键上一次状态，用于边沿触发。
  std::map<SDL_GameControllerButton, bool> last_buttons;
};

struct ControllerContext {
  SDL_GameController* controller = nullptr;
  SDL_Joystick* joystick = nullptr;
  SDL_JoystickID instance_id = -1;
  std::string name;
};

static FILE* g_log_file = nullptr;
static constexpr Sint16 kActiveAxisThreshold = 12000;
static constexpr float kTriggerPressedThreshold = 0.5f;
static constexpr int kModeStandingUp = 1;
static constexpr int kModeJointDamping = 2;
static constexpr int kModeRLControl = 6;
static constexpr int kModeLieDown = 18;

static float Clamp(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

static float ApplyDeadzone(float v, float deadzone) {
  return (std::abs(v) < deadzone) ? 0.0f : v;
}

static float NormalizeAxis(Sint16 value) {
  if (value < 0) {
    return static_cast<float>(value) / 32768.0f;
  }
  return static_cast<float>(value) / 32767.0f;
}

static bool ParseConfigLine(const std::string& line, std::string* key, std::string* value) {
  auto pos = line.find('=');
  if (pos == std::string::npos) {
    return false;
  }
  *key = line.substr(0, pos);
  *value = line.substr(pos + 1);
  return true;
}

static void Trim(std::string* s) {
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

static bool ParseBool(const std::string& value) {
  std::string v = value;
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return v == "1" || v == "true" || v == "yes" || v == "on";
}

static void LoadConfig(const std::string& path, Config* config) {
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
    } else if (key == "invert_left_horizontal_axis") {
      config->invert_left_horizontal_axis = ParseBool(value);
    } else if (key == "invert_horizontal_axis") {
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

static void LogLine(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::vfprintf(stderr, fmt, args);
  va_end(args);
  std::fflush(stderr);

  if (g_log_file) {
    va_start(args, fmt);
    std::vfprintf(g_log_file, fmt, args);
    va_end(args);
    std::fflush(g_log_file);
  }
}

static void ResetSnapshots(State* state, std::vector<int>* last_axes,
                           std::vector<int>* last_buttons, int* last_hat) {
  state->last_buttons.clear();
  last_axes->clear();
  last_buttons->clear();
  *last_hat = 0;
}

static void CloseController(ControllerContext* ctx) {
  if (ctx->controller) {
    SDL_GameControllerClose(ctx->controller);
  }
  ctx->controller = nullptr;
  ctx->joystick = nullptr;
  ctx->instance_id = -1;
  ctx->name.clear();
}

static bool OpenControllerByIndex(int device_index, ControllerContext* ctx) {
  if (!SDL_IsGameController(device_index)) {
    return false;
  }

  SDL_GameController* controller = SDL_GameControllerOpen(device_index);
  if (!controller) {
    LogLine("打开手柄[%d]失败: %s\n", device_index, SDL_GetError());
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

  LogLine("已打开手柄: index=%d instance_id=%d name=%s\n",
          device_index, static_cast<int>(instance_id), ctx->name.c_str());
  LogLine("手柄轴数量: %d, 按键数量: %d, Hat 数量: %d\n",
          SDL_JoystickNumAxes(joystick),
          SDL_JoystickNumButtons(joystick),
          SDL_JoystickNumHats(joystick));
  return true;
}

static ControllerContext* FindControllerByInstanceId(
    std::vector<ControllerContext>* controllers, SDL_JoystickID instance_id) {
  for (auto& controller : *controllers) {
    if (controller.instance_id == instance_id) {
      return &controller;
    }
  }
  return nullptr;
}

static bool OpenAllControllers(std::vector<ControllerContext>* controllers) {
  bool opened = false;
  const int joystick_count = SDL_NumJoysticks();
  LogLine("当前检测到手柄数量: %d\n", joystick_count);
  for (int i = 0; i < joystick_count; ++i) {
    const char* name = SDL_JoystickNameForIndex(i);
    LogLine("发现设备[%d]: %s game_controller=%d\n",
            i, name ? name : "unknown", SDL_IsGameController(i) ? 1 : 0);
    ControllerContext ctx;
    if (OpenControllerByIndex(i, &ctx)) {
      controllers->push_back(ctx);
      opened = true;
    }
  }
  return opened;
}

static bool AddControllerByIndex(int device_index, std::vector<ControllerContext>* controllers) {
  ControllerContext ctx;
  if (!OpenControllerByIndex(device_index, &ctx)) {
    return false;
  }
  if (FindControllerByInstanceId(controllers, ctx.instance_id)) {
    CloseController(&ctx);
    return false;
  }
  controllers->push_back(ctx);
  return true;
}

static void RemoveControllerByInstanceId(std::vector<ControllerContext>* controllers,
                                         SDL_JoystickID instance_id) {
  for (auto it = controllers->begin(); it != controllers->end(); ++it) {
    if (it->instance_id == instance_id) {
      CloseController(&(*it));
      controllers->erase(it);
      return;
    }
  }
}

static ControllerContext* ResolveActiveController(
    std::vector<ControllerContext>* controllers, SDL_JoystickID* active_instance_id) {
  ControllerContext* active = FindControllerByInstanceId(controllers, *active_instance_id);
  if (active) {
    return active;
  }
  if (controllers->empty()) {
    *active_instance_id = -1;
    return nullptr;
  }
  *active_instance_id = controllers->front().instance_id;
  LogLine("活动手柄切换为: instance_id=%d name=%s\n",
          static_cast<int>(*active_instance_id), controllers->front().name.c_str());
  return &controllers->front();
}

static void ActivateController(SDL_JoystickID instance_id,
                               std::vector<ControllerContext>* controllers,
                               SDL_JoystickID* active_instance_id) {
  if (*active_instance_id == instance_id) {
    return;
  }
  ControllerContext* controller = FindControllerByInstanceId(controllers, instance_id);
  if (!controller) {
    return;
  }
  *active_instance_id = instance_id;
  LogLine("活动手柄切换为: instance_id=%d name=%s\n",
          static_cast<int>(instance_id), controller->name.c_str());
}

static void LogRawState(SDL_Joystick* joystick, std::vector<int>* last_axes,
                        std::vector<int>* last_buttons, int* last_hat) {
  if (!joystick) {
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

  bool changed = false;
  std::string axis_line = "原始轴:";
  for (int i = 0; i < axes; ++i) {
    int raw = SDL_JoystickGetAxis(joystick, i);
    if (raw != (*last_axes)[i]) {
      changed = true;
      (*last_axes)[i] = raw;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), " a%d=%d", i, raw);
    axis_line += buf;
  }

  std::string button_line = "原始按键:";
  for (int i = 0; i < buttons; ++i) {
    int pressed = SDL_JoystickGetButton(joystick, i);
    if (pressed != (*last_buttons)[i]) {
      changed = true;
      (*last_buttons)[i] = pressed;
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), " b%d=%d", i, pressed);
    button_line += buf;
  }

  int hat = (hats > 0) ? SDL_JoystickGetHat(joystick, 0) : 0;
  if (hat != *last_hat) {
    changed = true;
    *last_hat = hat;
  }

  char hat_buf[16];
  std::snprintf(hat_buf, sizeof(hat_buf), " hat=%d", hat);
  if (changed) {
    LogLine("%s\n", axis_line.c_str());
    LogLine("%s%s\n", button_line.c_str(), hat_buf);
  }
}

static void HandleButtonEdge(State* state, const Config& config,
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
    state->roll = Clamp(state->roll - config.roll_step, config.roll_min, config.roll_max);
  } else if (button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
    state->roll = Clamp(state->roll + config.roll_step, config.roll_min, config.roll_max);
  }
}

static void HandleDpad(State* state, const Config& config,
                       SDL_GameControllerButton button, bool pressed) {
  if (!pressed) {
    return;
  }
  if (button == SDL_CONTROLLER_BUTTON_DPAD_UP) {
    state->height = Clamp(state->height + config.height_step, config.height_min, config.height_max);
  } else if (button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) {
    state->height = Clamp(state->height - config.height_step, config.height_min, config.height_max);
  } else if (button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
    state->pitch = Clamp(state->pitch + config.pitch_step, config.pitch_min, config.pitch_max);
  } else if (button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
    state->pitch = Clamp(state->pitch - config.pitch_step, config.pitch_min, config.pitch_max);
  }
}

int main(int argc, char** argv) {
  Config config;
  if (argc > 1) {
    LoadConfig(argv[1], &config);
  } else if (FileExists("config.txt")) {
    LoadConfig("config.txt", &config);
  }

  if (!config.log_path.empty()) {
    g_log_file = std::fopen(config.log_path.c_str(), "a");
    if (!g_log_file) {
      std::fprintf(stderr, "无法打开日志文件: %s\n", config.log_path.c_str());
    }
  }

  LogLine("启动 Linux 手柄发送器... 版本=%s\n", kVersion);
  LogLine("配置: debug=%d raw_dump=%d print_every=%d log_path=%s\n",
          config.debug ? 1 : 0, config.raw_dump ? 1 : 0, config.print_every,
          config.log_path.c_str());

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
    LogLine("SDL 初始化失败: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow("joystick_sender_linux",
                                        SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED,
                                        320, 200,
                                        SDL_WINDOW_HIDDEN);
  if (!window) {
    LogLine("创建 SDL 窗口失败: %s\n", SDL_GetError());
  }
  SDL_GameControllerEventState(SDL_ENABLE);

  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    LogLine("创建 UDP socket 失败: %s\n", std::strerror(errno));
    if (window) {
      SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(config.port));
  if (inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr) != 1) {
    LogLine("无效的 UDP 目标地址: %s\n", config.host.c_str());
    close(sock);
    if (window) {
      SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return 1;
  }
  LogLine("UDP 目标: %s:%d\n", config.host.c_str(), config.port);

  State state;
  state.height = config.height_min + (config.height_max - config.height_min) * 0.5f;
  state.mode = -1;
  state.gait = config.gait_min;

  std::vector<ControllerContext> controllers;
  SDL_JoystickID active_instance_id = -1;
  std::vector<int> last_axes;
  std::vector<int> last_raw_buttons;
  int last_hat = 0;
  OpenAllControllers(&controllers);
  if (controllers.empty()) {
    LogLine("启动时未发现可用手柄，进入等待状态。\n");
  } else {
    active_instance_id = controllers.front().instance_id;
    LogLine("初始活动手柄: instance_id=%d name=%s\n",
            static_cast<int>(active_instance_id), controllers.front().name.c_str());
  }

  const int interval_ms = std::max(1, 1000 / std::max(1, config.send_hz));
  uint32_t last_send = SDL_GetTicks();
  uint32_t last_retry = last_send;
  int send_count = 0;
  bool running = true;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
        LogLine("检测到手柄接入: device_index=%d\n", event.cdevice.which);
        if (AddControllerByIndex(event.cdevice.which, &controllers) && active_instance_id < 0) {
          active_instance_id = controllers.back().instance_id;
          ResetSnapshots(&state, &last_axes, &last_raw_buttons, &last_hat);
        }
      } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        LogLine("检测到手柄移除: instance_id=%d\n", static_cast<int>(event.cdevice.which));
        const bool was_active = (event.cdevice.which == active_instance_id);
        RemoveControllerByInstanceId(&controllers, event.cdevice.which);
        if (was_active) {
          LogLine("当前活动手柄已断开，清空状态并等待新的活动输入。\n");
          ResetSnapshots(&state, &last_axes, &last_raw_buttons, &last_hat);
          active_instance_id = -1;
        }
      } else if (event.type == SDL_CONTROLLERBUTTONDOWN ||
                 event.type == SDL_CONTROLLERBUTTONUP) {
        ControllerContext* controller = FindControllerByInstanceId(&controllers, event.cbutton.which);
        if (!controller) {
          continue;
        }
        ActivateController(event.cbutton.which, &controllers, &active_instance_id);
        SDL_GameControllerButton button =
            static_cast<SDL_GameControllerButton>(event.cbutton.button);
        bool pressed = (event.cbutton.state == SDL_PRESSED);
        if (config.debug) {
          LogLine("按键事件: instance_id=%d button=%d pressed=%d\n",
                  static_cast<int>(event.cbutton.which),
                  static_cast<int>(button), pressed ? 1 : 0);
        }
        HandleButtonEdge(&state, config, button, pressed);
        HandleDpad(&state, config, button, pressed);
      } else if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (std::abs(event.caxis.value) >= kActiveAxisThreshold) {
          ActivateController(event.caxis.which, &controllers, &active_instance_id);
        }
      }
    }

    uint32_t now = SDL_GetTicks();
    ControllerContext* active_controller = ResolveActiveController(&controllers, &active_instance_id);
    if (!active_controller && now - last_retry >= 1000) {
      OpenAllControllers(&controllers);
      active_controller = ResolveActiveController(&controllers, &active_instance_id);
      if (active_controller) {
        ResetSnapshots(&state, &last_axes, &last_raw_buttons, &last_hat);
      }
      last_retry = now;
    }

    if (active_controller && now - last_send >= static_cast<uint32_t>(interval_ms)) {
      SDL_PumpEvents();
      SDL_GameControllerUpdate();
      SDL_JoystickUpdate();

      float lx = NormalizeAxis(SDL_GameControllerGetAxis(active_controller->controller, SDL_CONTROLLER_AXIS_LEFTX));
      float ly = NormalizeAxis(SDL_GameControllerGetAxis(active_controller->controller, SDL_CONTROLLER_AXIS_LEFTY));
      float rx = NormalizeAxis(SDL_GameControllerGetAxis(active_controller->controller, SDL_CONTROLLER_AXIS_RIGHTX));
      float ry = NormalizeAxis(SDL_GameControllerGetAxis(active_controller->controller, SDL_CONTROLLER_AXIS_RIGHTY));
      float lt = SDL_GameControllerGetAxis(active_controller->controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
      float rt = SDL_GameControllerGetAxis(active_controller->controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;

      lx = ApplyDeadzone(lx, config.axis_deadzone);
      ly = ApplyDeadzone(ly, config.axis_deadzone);
      rx = ApplyDeadzone(rx, config.axis_deadzone);
      ry = ApplyDeadzone(ry, config.axis_deadzone);

      if (config.invert_left_horizontal_axis) {
        lx = -lx;
      }

      float vx = -ly * config.vx_max;
      float vy = lx * config.vy_max;
      float wz = -rx * config.wz_max;

      bool lb = SDL_GameControllerGetButton(active_controller->controller,
                                            SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
      bool rb = SDL_GameControllerGetButton(active_controller->controller,
                                            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
      if (lb && rb) {
        state.mode = kModeRLControl;
      } else if (lt >= kTriggerPressedThreshold && rt >= kTriggerPressedThreshold) {
        state.mode = kModeJointDamping;
      }

      char buffer[256];
      std::snprintf(buffer, sizeof(buffer), "CMD %.3f %.3f %.3f %.3f %.3f %.3f %d %d",
                    vx, vy, wz, state.height, state.pitch, state.roll, state.mode, state.gait);

      ssize_t result = sendto(sock, buffer, std::strlen(buffer), 0,
                              reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
      if (result < 0) {
        LogLine("发送失败: %s\n", std::strerror(errno));
      }

      send_count += 1;
      if (config.debug && (send_count % config.print_every == 0)) {
        LogLine("发送数据: %s\n", buffer);
        LogLine("轴: lx=%.3f ly=%.3f rx=%.3f ry=%.3f lt=%.3f rt=%.3f\n",
                lx, ly, rx, ry, lt, rt);
        if (config.raw_dump && active_controller->joystick) {
          LogRawState(active_controller->joystick, &last_axes, &last_raw_buttons, &last_hat);
        }
      }

      last_send = now;
    }

    SDL_Delay(1);
  }

  for (auto& controller : controllers) {
    CloseController(&controller);
  }
  close(sock);
  if (window) {
    SDL_DestroyWindow(window);
  }
  SDL_Quit();
  if (g_log_file) {
    std::fclose(g_log_file);
  }
  return 0;
}
