import socket
import sys

def run_client(server_host, server_port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(5)  # Увеличил таймаут для реальных сетей
    
    print(f"Подключаемся к серверу {server_host}:{server_port}")
    
    # Отправляем два пробных сообщения
    sock.sendto(b'\x00', (server_host, server_port))
    print("Отправлено первое сообщение")
    sock.sendto(b'\x01', (server_host, server_port))
    print("Отправлено второе сообщение")

    try:
        data, addr = sock.recvfrom(1024)
        print(f"Получено сообщение от {addr}: {data.decode()}")
    except socket.timeout:
        print("Таймаут соединения - не удалось получить ответ от сервера")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Использование: python client.py <server_host> <server_port>")
        print("Пример: python client.py example.com 12345")
        sys.exit(1)
    
    server_host = sys.argv[1]
    server_port = int(sys.argv[2])
    run_client(server_host, server_port)
