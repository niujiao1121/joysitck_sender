#include <SDL2/SDL.h>

#include "joystick_sender_common.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace joystick_sender;

static FILE* g_log_file = nullptr;
static bool g_terminal_ui_active = false;

static void LogLine(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (!g_terminal_ui_active) {
    std::vfprintf(stderr, fmt, args);
  }
  va_end(args);
  if (!g_terminal_ui_active) {
    std::fflush(stderr);
  }

  if (g_log_file) {
    va_start(args, fmt);
    std::vfprintf(g_log_file, fmt, args);
    va_end(args);
    std::fflush(g_log_file);
  }
}

static void InitTerminalUi(UiPanel* ui) {
  ui->enabled = isatty(STDERR_FILENO);
  if (!ui->enabled) {
    return;
  }
  g_terminal_ui_active = true;
  std::fprintf(stderr, "\033[?25l\033[2J\033[H");
  std::fflush(stderr);
}

static void ShutdownTerminalUi(UiPanel* ui) {
  if (!ui->enabled) {
    return;
  }
  std::fprintf(stderr, "\033[?25h\n");
  std::fflush(stderr);
  g_terminal_ui_active = false;
}

static void RenderTerminalUi(UiPanel* ui, const State& state, uint32_t now_ms) {
  if (!ui->enabled) {
    return;
  }
  if (!ui->first_render && now_ms - ui->last_render_ms < 50) {
    return;
  }
  ui->first_render = false;
  ui->last_render_ms = now_ms;
  std::fprintf(stderr,
               "\033[H"
               "joystick_sender_linux v%s\033[K\n"
               "状态: %s\033[K\n"
               "手柄: %s\033[K\n"
               "发送: ok=%d fail=%d\033[K\n"
               "发送数据: %s\033[K\n"
               "轴: %s\033[K\n"
               "%s\033[K\n"
               "%s\033[K\n"
               "姿态: height=%.3f pitch=%.3f roll=%.3f mode=%d gait=%d\033[K\n"
               "最近事件: %s\033[K\n"
               "按 Ctrl+C 或关闭窗口退出\033[K\n"
               "\033[J",
               kVersion,
               ui->status.c_str(),
               ui->controller.c_str(),
               ui->send_count,
               ui->send_fail_count,
               ui->command.c_str(),
               ui->axes.c_str(),
               ui->raw_axes.c_str(),
               ui->raw_buttons.c_str(),
               state.height,
               state.pitch,
               state.roll,
               state.mode,
               state.gait,
               ui->event.c_str());
  std::fflush(stderr);
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

  UiPanel ui;
  InitTerminalUi(&ui);
  ui.status = "初始化";

  LogLine("启动 Linux 手柄发送器... 版本=%s\n", kVersion);
  LogLine("配置: debug=%d raw_dump=%d print_every=%d log_path=%s\n",
          config.debug ? 1 : 0, config.raw_dump ? 1 : 0, config.print_every,
          config.log_path.c_str());

  LogLine("操作方式:\n");
  LogLine("  左摇杆 X/Y -> vx/vy\n");
  LogLine("  右摇杆 X -> wz\n");
  LogLine("  十字键 上/下 -> height 增减\n");
  LogLine("  十字键 左/右 -> roll 增减\n");
  LogLine("  LB/RB -> pitch 增减\n");
  LogLine("  A -> mode=1 StandingUp，B -> mode=18 LieDown\n");
  LogLine("  LB+RB -> mode=6 RLControl，LT+RT -> mode=2 JointDamping\n");
  LogLine("  X/Y -> gait 0/1\n");

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
    LogLine("SDL 初始化失败: %s\n", SDL_GetError());
    ShutdownTerminalUi(&ui);
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
    ShutdownTerminalUi(&ui);
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
    ShutdownTerminalUi(&ui);
    return 1;
  }
  LogLine("UDP 目标: %s:%d\n", config.host.c_str(), config.port);

  State state;
  state.height = config.height_min + (config.height_max - config.height_min) * 0.5f;
  state.mode = -1;
  state.gait = std::min(config.gait_max, std::max(config.gait_min, config.gait_default));

  std::vector<ControllerContext> controllers;
  SDL_JoystickID active_instance_id = -1;
  std::vector<int> last_axes;
  std::vector<int> last_raw_buttons;
  int last_hat = 0;
  OpenAllControllers(&controllers, LogLine);
  if (controllers.empty()) {
    ui.status = "等待手柄";
    ui.event = "启动时未发现可用手柄";
    LogLine("启动时未发现可用手柄，进入等待状态。\n");
  } else {
    active_instance_id = controllers.front().instance_id;
    ui.status = "运行中";
    ui.controller = FormatController(&controllers.front());
    ui.event = "初始活动手柄已选择";
    LogLine("初始活动手柄: instance_id=%d name=%s\n",
            static_cast<int>(active_instance_id), controllers.front().name.c_str());
  }

  const int interval_ms = std::max(1, 1000 / std::max(1, config.send_hz));
  uint32_t last_send = SDL_GetTicks();
  uint32_t last_retry = last_send;
  int send_count = 0;
  bool running = true;

  while (running) {
    uint32_t now = SDL_GetTicks();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
        char event_buf[128];
        std::snprintf(event_buf, sizeof(event_buf), "手柄接入: device_index=%d", event.cdevice.which);
        ui.event = event_buf;
        LogLine("检测到手柄接入: device_index=%d\n", event.cdevice.which);
        if (AddControllerByIndex(event.cdevice.which, &controllers, LogLine) && active_instance_id < 0) {
          active_instance_id = controllers.back().instance_id;
          ResetSnapshots(&state, &last_axes, &last_raw_buttons, &last_hat);
          ui.status = "运行中";
          ui.controller = FormatController(&controllers.back());
        }
      } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        char event_buf[128];
        std::snprintf(event_buf, sizeof(event_buf), "手柄移除: instance_id=%d",
                      static_cast<int>(event.cdevice.which));
        ui.event = event_buf;
        LogLine("检测到手柄移除: instance_id=%d\n", static_cast<int>(event.cdevice.which));
        const bool was_active = (event.cdevice.which == active_instance_id);
        RemoveControllerByInstanceId(&controllers, event.cdevice.which);
        if (was_active) {
          LogLine("当前活动手柄已断开，清空状态并等待新的活动输入。\n");
          ResetSnapshots(&state, &last_axes, &last_raw_buttons, &last_hat);
          active_instance_id = -1;
          ui.status = "等待手柄";
          ui.controller = "none";
        }
      } else if (event.type == SDL_CONTROLLERBUTTONDOWN ||
                 event.type == SDL_CONTROLLERBUTTONUP) {
        ControllerContext* controller = FindControllerByInstanceId(&controllers, event.cbutton.which);
        if (!controller) {
          continue;
        }
        ActivateController(event.cbutton.which, &controllers, &active_instance_id, LogLine);
        SDL_GameControllerButton button =
            static_cast<SDL_GameControllerButton>(event.cbutton.button);
        bool pressed = (event.cbutton.state == SDL_PRESSED);
        char event_buf[160];
        std::snprintf(event_buf, sizeof(event_buf),
                      "按键事件: instance_id=%d button=%d pressed=%d",
                      static_cast<int>(event.cbutton.which),
                      static_cast<int>(button), pressed ? 1 : 0);
        ui.event = event_buf;
        if (config.debug) {
          LogLine("按键事件: instance_id=%d button=%d pressed=%d\n",
                  static_cast<int>(event.cbutton.which),
                  static_cast<int>(button), pressed ? 1 : 0);
        }
        HandleButtonEdge(&state, config, button, pressed);
        HandleDpad(&state, config, button, pressed);
      } else if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (std::abs(event.caxis.value) >= kActiveAxisThreshold) {
          ActivateController(event.caxis.which, &controllers, &active_instance_id, LogLine);
          char event_buf[160];
          std::snprintf(event_buf, sizeof(event_buf),
                        "轴事件: instance_id=%d axis=%d value=%d",
                        static_cast<int>(event.caxis.which),
                        static_cast<int>(event.caxis.axis),
                        static_cast<int>(event.caxis.value));
          ui.event = event_buf;
        }
      }
    }

    ControllerContext* active_controller = ResolveActiveController(&controllers, &active_instance_id, LogLine);
    ui.controller = FormatController(active_controller);
    if (!active_controller && now - last_retry >= 1000) {
      OpenAllControllers(&controllers, LogLine);
      active_controller = ResolveActiveController(&controllers, &active_instance_id, LogLine);
      if (active_controller) {
        ResetSnapshots(&state, &last_axes, &last_raw_buttons, &last_hat);
        ui.status = "运行中";
        ui.controller = FormatController(active_controller);
        ui.event = "重新检测到可用手柄";
      } else {
        ui.status = "等待手柄";
      }
      last_retry = now;
    }

    if (active_controller && now - last_send >= static_cast<uint32_t>(interval_ms)) {
      SDL_PumpEvents();
      SDL_GameControllerUpdate();
      SDL_JoystickUpdate();

      CommandFrame frame = BuildCommandFrame(active_controller->controller, config, &state);
      ui.command = frame.command;
      ui.axes = frame.axes;
      if (config.raw_dump && active_controller->joystick) {
        UpdateRawState(active_controller->joystick, &ui, &last_axes, &last_raw_buttons, &last_hat);
      }

	      ssize_t result = sendto(sock, frame.command.c_str(), frame.command.size(), 0,
	                              reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
	      if (result < 0) {
        ui.send_fail_count += 1;
        ui.status = "发送失败";
	        LogLine("发送失败: %s\n", std::strerror(errno));
      } else {
        ui.status = "运行中";
	      }

	      send_count += 1;
      ui.send_count = send_count;
	      if (config.debug && (send_count % config.print_every == 0)) {
        if (!ui.enabled) {
          LogLine("发送数据: %s\n", frame.command.c_str());
          LogLine("轴: %s\n", frame.axes.c_str());
        }
	        if (config.raw_dump && active_controller->joystick) {
          if (!ui.enabled) {
            LogRawState(active_controller->joystick, &last_axes, &last_raw_buttons, &last_hat, LogLine);
          }
	        }
	      }

	      last_send = now;
	    }

    RenderTerminalUi(&ui, state, now);
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
  ShutdownTerminalUi(&ui);
  if (g_log_file) {
    std::fclose(g_log_file);
  }
  return 0;
}
