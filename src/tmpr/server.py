import socket
import sys

def run_server(port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', port))
    print(f"Сервер запущен на порту {port}")

    message_count = {}  # Словарь для подсчета сообщений от каждого клиента

    while True:
        data, addr = sock.recvfrom(1)
        
        # Увеличиваем счетчик сообщений для этого клиента
        message_count[addr] = message_count.get(addr, 0) + 1
        print(f"Получено сообщение #{message_count[addr]} от {addr}")

        # Если клиент отправил 2 сообщения, отправляем ответ
        if message_count[addr] >= 2:
            print(f"Отправляем 'Hello World!' клиенту {addr}")
            sock.sendto(b"Hello World!", addr)
            # Сбрасываем счетчик для этого клиента
            message_count[addr] = 0

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Использование: python server.py <port>")
        sys.exit(1)
    
    port = int(sys.argv[1])
    run_server(port)
