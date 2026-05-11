#include <SDL2/SDL.h>
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

static const char* kVersion = "0.1.0-retroid";

struct Config {
  std::string host = "127.0.0.1";
  int port = 12121;
  int send_hz = 50;
  bool debug = false;
  int print_every = 20;
  std::string log_path = "retroid_sender.log";
  bool raw_dump = false;
  float axis_deadzone = 0.05f;
};

struct State {
  std::map<SDL_GameControllerButton, bool> last_buttons;
};

static FILE* g_log_file = nullptr;

static void EnsureUtf8Bom(FILE* file) {
  if (!file) {
    return;
  }
  long pos = std::ftell(file);
  if (pos == 0) {
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    std::fwrite(bom, 1, 3, file);
    std::fflush(file);
  }
}

static void LogLine(const char* fmt, ...) {
  HANDLE console = GetStdHandle(STD_ERROR_HANDLE);
  DWORD mode = 0;
  bool has_console = (console != INVALID_HANDLE_VALUE) && GetConsoleMode(console, &mode);

  va_list args;
  va_start(args, fmt);
  char utf8_buf[2048];
  std::vsnprintf(utf8_buf, sizeof(utf8_buf), fmt, args);
  va_end(args);

  if (has_console) {
    int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8_buf, -1, nullptr, 0);
    if (wide_len > 0) {
      std::wstring wide_buf(wide_len, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, utf8_buf, -1, &wide_buf[0], wide_len);
      DWORD written = 0;
      WriteConsoleW(console, wide_buf.c_str(),
                    static_cast<DWORD>(wcslen(wide_buf.c_str())), &written, nullptr);
    } else {
      std::fputs(utf8_buf, stderr);
    }
  } else {
    std::fputs(utf8_buf, stderr);
  }

  if (g_log_file) {
    std::fputs(utf8_buf, g_log_file);
    std::fflush(g_log_file);
  }
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

static bool ParseConfigLine(const std::string& line, std::string* key, std::string* value) {
  auto pos = line.find('=');
  if (pos == std::string::npos) {
    return false;
  }
  *key = line.substr(0, pos);
  *value = line.substr(pos + 1);
  return true;
}

static void LoadConfig(const std::string& path, Config* config) {
  std::ifstream file(path);
  if (!file.is_open()) {
    LogLine("配置文件未找到，使用默认配置: %s\n", path.c_str());
    return;
  }
  LogLine("读取配置文件: %s\n", path.c_str());
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
    LogLine("配置项: %s=%s\n", key.c_str(), value.c_str());
    if (key == "udp_host") {
      config->host = value;
    } else if (key == "udp_port") {
      config->port = std::stoi(value);
    } else if (key == "send_hz") {
      config->send_hz = std::stoi(value);
    } else if (key == "debug") {
      config->debug = (value == "1" || value == "true" || value == "TRUE");
    } else if (key == "print_every") {
      config->print_every = std::max(1, std::stoi(value));
    } else if (key == "log_path") {
      config->log_path = value;
    } else if (key == "raw_dump") {
      config->raw_dump = (value == "1" || value == "true" || value == "TRUE");
    } else if (key == "axis_deadzone") {
      config->axis_deadzone = std::stof(value);
    }
  }
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

static int16_t ToAxis1000(float v) {
  float clamped = std::max(-1.0f, std::min(1.0f, v));
  return static_cast<int16_t>(std::round(clamped * 1000.0f));
}

static uint16_t CalcCrc16(const uint8_t* data, uint16_t len) {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < len; ++i) {
    sum += data[i];
  }
  return static_cast<uint16_t>(sum & 0xFFFF);
}

static void SetBit(int16_t* ch, int bit, bool on) {
  if (bit < 0 || bit > 15) {
    return;
  }
  ch[bit] = on ? 1 : 0;
}

int main(int argc, char** argv) {
  Config config;
  LoadConfig("config.txt", &config);
  if (argc > 2) {
    config.host = argv[1];
    config.port = std::atoi(argv[2]);
  }

  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  if (!config.log_path.empty()) {
    g_log_file = std::fopen(config.log_path.c_str(), "a");
    if (!g_log_file) {
      LogLine("无法打开日志文件: %s\n", config.log_path.c_str());
    } else {
      EnsureUtf8Bom(g_log_file);
    }
  }

  LogLine("启动中... 版本=%s\n", kVersion);
  LogLine("配置: debug=%d raw_dump=%d print_every=%d log_path=%s\n",
          config.debug ? 1 : 0, config.raw_dump ? 1 : 0, config.print_every,
          config.log_path.c_str());

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
    LogLine("SDL 初始化失败: %s\n", SDL_GetError());
    return 1;
  }
  LogLine("SDL 初始化完成。\n");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_GameControllerEventState(SDL_ENABLE);

  SDL_Window* window = SDL_CreateWindow(
      "retroid_sender",
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      320,
      200,
      SDL_WINDOW_HIDDEN);
  if (!window) {
    LogLine("创建 SDL 窗口失败: %s\n", SDL_GetError());
  }

  SDL_GameController* controller = nullptr;
  SDL_Joystick* joystick = nullptr;
  int joystick_count = SDL_NumJoysticks();
  LogLine("检测到手柄数量: %d\n", joystick_count);
  for (int i = 0; i < joystick_count; ++i) {
    if (SDL_IsGameController(i)) {
      const char* name = SDL_GameControllerNameForIndex(i);
      LogLine("发现手柄[%d]: %s\n", i, name ? name : "unknown");
      controller = SDL_GameControllerOpen(i);
      if (controller) {
        joystick = SDL_GameControllerGetJoystick(controller);
        break;
      }
    }
  }

  if (!controller) {
    LogLine("未发现可用手柄，请确认已连接。\n");
    SDL_Quit();
    return 1;
  }
  LogLine("已打开手柄: %s\n", SDL_GameControllerName(controller));
  if (joystick) {
    LogLine("手柄轴数量: %d, 按键数量: %d, Hat 数量: %d\n",
            SDL_JoystickNumAxes(joystick),
            SDL_JoystickNumButtons(joystick),
            SDL_JoystickNumHats(joystick));
  }

  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    LogLine("WSAStartup 失败。\n");
    SDL_GameControllerClose(controller);
    SDL_Quit();
    return 1;
  }

  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    LogLine("创建 UDP socket 失败。\n");
    WSACleanup();
    SDL_GameControllerClose(controller);
    SDL_Quit();
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(config.port));
  addr.sin_addr.s_addr = inet_addr(config.host.c_str());
  LogLine("UDP 目标: %s:%d\n", config.host.c_str(), config.port);

  const int interval_ms = std::max(1, 1000 / std::max(1, config.send_hz));
  LogLine("发送频率: %d Hz, 间隔: %d ms\n", config.send_hz, interval_ms);

  uint16_t seq = 0;
  int send_count = 0;
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      }
    }

    SDL_PumpEvents();
    SDL_GameControllerUpdate();
    SDL_JoystickUpdate();

    float lx = ApplyDeadzone(NormalizeAxis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX)),
                             config.axis_deadzone);
    float ly = ApplyDeadzone(NormalizeAxis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY)),
                             config.axis_deadzone);
    float rx = ApplyDeadzone(NormalizeAxis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX)),
                             config.axis_deadzone);
    float ry = ApplyDeadzone(NormalizeAxis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY)),
                             config.axis_deadzone);

    bool a = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A);
    bool b = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B);
    bool x = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_X);
    bool y = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_Y);
    bool lb = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    bool rb = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    bool start = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START);
    bool back = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
    bool l3 = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSTICK);
    bool r3 = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    bool dpad_left = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    bool dpad_right = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    bool dpad_up = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP);
    bool dpad_down = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);

    float lt = (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f);
    float rt = (SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f);
    bool l2 = lt > 0.5f;
    bool r2 = rt > 0.5f;

    int16_t ch[16] = {0};
    SetBit(ch, 0, rb);        // R1
    SetBit(ch, 1, lb);        // L1
    SetBit(ch, 2, start);     // start
    SetBit(ch, 3, back);      // select
    SetBit(ch, 4, r2);        // R2
    SetBit(ch, 5, l2);        // L2
    SetBit(ch, 6, a);         // A
    SetBit(ch, 7, b);         // B
    SetBit(ch, 8, x);         // X
    SetBit(ch, 9, y);         // Y
    SetBit(ch, 10, dpad_left);
    SetBit(ch, 11, dpad_right);
    SetBit(ch, 12, dpad_up);
    SetBit(ch, 13, dpad_down);
    SetBit(ch, 14, l3);
    SetBit(ch, 15, r3);

    int16_t left_axis_x = ToAxis1000(lx);
    int16_t left_axis_y = ToAxis1000(ly);
    int16_t right_axis_x = ToAxis1000(rx);
    int16_t right_axis_y = ToAxis1000(ry);

    std::uint8_t payload[42];
    std::memset(payload, 0, sizeof(payload));
    payload[0] = 0x55;
    payload[1] = 0x66;
    payload[2] = 0x00;  // ctrl
    std::uint16_t data_len = 32;
    std::uint16_t seq_local = seq++;
    payload[3] = static_cast<std::uint8_t>(data_len & 0xFF);
    payload[4] = static_cast<std::uint8_t>((data_len >> 8) & 0xFF);
    payload[5] = static_cast<std::uint8_t>(seq_local & 0xFF);
    payload[6] = static_cast<std::uint8_t>((seq_local >> 8) & 0xFF);
    payload[7] = 0x01;  // Retroid id

    std::uint8_t* data = payload + 10;
    std::memcpy(data, ch, sizeof(ch));
    std::memcpy(data + 20, &left_axis_x, sizeof(left_axis_x));
    std::memcpy(data + 22, &left_axis_y, sizeof(left_axis_y));
    std::memcpy(data + 24, &right_axis_x, sizeof(right_axis_x));
    std::memcpy(data + 26, &right_axis_y, sizeof(right_axis_y));
    std::uint16_t axis_buttons[2] = {0, 0};
    std::memcpy(data + 28, axis_buttons, sizeof(axis_buttons));

    std::uint16_t crc16 = CalcCrc16(data, data_len);
    payload[8] = static_cast<std::uint8_t>(crc16 & 0xFF);
    payload[9] = static_cast<std::uint8_t>((crc16 >> 8) & 0xFF);

    int result = sendto(sock, reinterpret_cast<const char*>(payload), sizeof(payload), 0,
                        reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result == SOCKET_ERROR) {
      LogLine("发送失败，错误码: %d\n", WSAGetLastError());
    }
    send_count += 1;
    if (config.debug && (send_count % config.print_every == 0)) {
      LogLine("发送 retroid 包: seq=%u crc=0x%04X lx=%d ly=%d rx=%d ry=%d\n",
              seq_local, crc16, left_axis_x, left_axis_y, right_axis_x, right_axis_y);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }

  closesocket(sock);
  WSACleanup();
  SDL_GameControllerClose(controller);
  if (window) {
    SDL_DestroyWindow(window);
  }
  SDL_Quit();
  if (g_log_file) {
    std::fclose(g_log_file);
  }
  return 0;
}
