#include <winsock2.h>
#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

static const char* kVersion = "0.1.0";

struct Config {
  std::string host = "127.0.0.1";
  int port = 12121;
  int send_hz = 50;
  bool debug = false;
  int print_every = 20;
  std::string log_path = "keyboard_sender.log";
  bool raw_dump = false;
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

static float Clamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static bool KeyDown(int vk) {
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

int main(int argc, char** argv) {
  Config config;
  const std::string default_config = "config.txt";
  LoadConfig(default_config, &config);
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

  WSADATA wsa_data;
  if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
    LogLine("WSAStartup 失败。\n");
    return 1;
  }

  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    LogLine("创建 UDP socket 失败。\n");
    WSACleanup();
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(config.port));
  addr.sin_addr.s_addr = inet_addr(config.host.c_str());

  float height = (config.height_min + config.height_max) * 0.5f;
  float pitch = 0.0f;
  float roll = 0.0f;
  int mode = config.mode_min;
  int gait = config.gait_min;

  const int interval_ms = std::max(1, 1000 / std::max(1, config.send_hz));

  LogLine("键盘控制启动:\n");
  LogLine("  WASD=左摇杆(vx/vy)\n");
  LogLine("  Q/E=右摇杆X(wz)\n");
  LogLine("  方向键=十字键(height/pitch)\n");
  LogLine("  Z/X=LB/RB(roll)\n");
  LogLine("  1/2=A/B(mode)\n");
  LogLine("  3/4=X/Y(gait)\n");
  LogLine("发送目标: %s:%d\n", config.host.c_str(), config.port);

  bool last_1 = false, last_2 = false, last_3 = false, last_4 = false;
  bool last_z = false, last_x = false;

  int send_count = 0;
  while (true) {
    float vx = 0.0f;
    float vy = 0.0f;
    float wz = 0.0f;

    // WASD 对标左摇杆：W=前进(正 vx), S=后退(负 vx), A=左移(负 vy), D=右移(正 vy)
    if (KeyDown('W')) vx += config.vx_max;
    if (KeyDown('S')) vx -= config.vx_max;
    if (KeyDown('D')) vy += config.vy_max;
    if (KeyDown('A')) vy -= config.vy_max;
    // Q/E 对标右摇杆 X：Q=左转(正 wz), E=右转(负 wz)
    if (KeyDown('Q')) wz += config.wz_max;
    if (KeyDown('E')) wz -= config.wz_max;

    if (KeyDown(VK_UP)) {
      height = Clamp(height + config.height_step, config.height_min, config.height_max);
    }
    if (KeyDown(VK_DOWN)) {
      height = Clamp(height - config.height_step, config.height_min, config.height_max);
    }
    if (KeyDown(VK_RIGHT)) {
      pitch = Clamp(pitch + config.pitch_step, config.pitch_min, config.pitch_max);
    }
    if (KeyDown(VK_LEFT)) {
      pitch = Clamp(pitch - config.pitch_step, config.pitch_min, config.pitch_max);
    }

    bool now_z = KeyDown('Z');
    bool now_x = KeyDown('X');
    if (now_z && !last_z) {
      roll = Clamp(roll - config.roll_step, config.roll_min, config.roll_max);
    }
    if (now_x && !last_x) {
      roll = Clamp(roll + config.roll_step, config.roll_min, config.roll_max);
    }
    last_z = now_z;
    last_x = now_x;

    bool now_1 = KeyDown('1');
    bool now_2 = KeyDown('2');
    bool now_3 = KeyDown('3');
    bool now_4 = KeyDown('4');
    if (now_1 && !last_1) mode = std::min(config.mode_max, mode + 1);
    if (now_2 && !last_2) mode = std::max(config.mode_min, mode - 1);
    if (now_3 && !last_3) gait = std::min(config.gait_max, gait + 1);
    if (now_4 && !last_4) gait = std::max(config.gait_min, gait - 1);
    last_1 = now_1;
    last_2 = now_2;
    last_3 = now_3;
    last_4 = now_4;

    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "CMD %.3f %.3f %.3f %.3f %.3f %.3f %d %d",
                  vx, vy, wz, height, pitch, roll, mode, gait);
    int result = sendto(sock, buffer, static_cast<int>(std::strlen(buffer)), 0,
                        reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result == SOCKET_ERROR) {
      LogLine("发送失败，错误码: %d\n", WSAGetLastError());
    }
    send_count += 1;
    if (config.debug && (send_count % config.print_every == 0)) {
      LogLine("发送数据: %s\n", buffer);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }

  closesocket(sock);
  WSACleanup();
  return 0;
}
