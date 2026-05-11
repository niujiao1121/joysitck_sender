#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>

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
}

static const char* kVersion = "0.1.0";

int main(int argc, char** argv) {
  int port = 12121;
  if (argc > 1) {
    port = std::atoi(argv[1]);
  }

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
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
    LogLine("绑定端口失败: %d\n", WSAGetLastError());
    closesocket(sock);
    WSACleanup();
    return 1;
  }

  LogLine("开始监听 UDP 端口: %d 版本=%s\n", port, kVersion);

  char buffer[2048];
  sockaddr_in from{};
  int from_len = sizeof(from);

  while (true) {
    int received = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                            reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received == SOCKET_ERROR) {
      LogLine("接收失败: %d\n", WSAGetLastError());
      continue;
    }
    buffer[received] = '\0';
    LogLine("%s:%d %s\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port), buffer);
  }

  closesocket(sock);
  WSACleanup();
  return 0;
}
