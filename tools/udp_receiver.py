import argparse
import socket


def main() -> None:
    parser = argparse.ArgumentParser(description="UDP 接收测试工具")
    parser.add_argument("--host", default="0.0.0.0", help="监听地址")
    parser.add_argument("--port", type=int, default=12121, help="监听端口")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))

    print(f"开始监听 UDP {args.host}:{args.port}")
    while True:
        data, addr = sock.recvfrom(2048)
        try:
            text = data.decode("ascii", errors="replace")
        except UnicodeDecodeError:
            text = str(data)
        print(f"[{addr[0]}:{addr[1]}] {text}")


if __name__ == "__main__":
    main()
