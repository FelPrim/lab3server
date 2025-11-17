import socket
import struct
import time
import threading

def test_basic_communication():
    """Тест базового подключения и получения сообщений от сервера"""
    print("=== Тест базового подключения ===")
    
    try:
        # Подключаемся к серверу
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect(('localhost', 23231))
        print("✓ Успешное подключение к серверу")
        
        # Отправляем UDP-адрес (обязательно для работы)
        udp_payload = struct.pack('!HHI8s', 2, 12345, 0x7F000001, b'\x00' * 8)  # AF_INET=2, порт=12345, IP=127.0.0.1
        message = b'\x01' + udp_payload  # CLIENT_UDP_ADDR
        sock.send(message)
        print("✓ Отправлен UDP-адрес")
        
        # Запрашиваем создание стрима
        sock.send(b'\x03')  # CLIENT_STREAM_CREATE
        print("✓ Отправлен запрос на создание стрима")
        
        # Ждем ответ от сервера
        response = sock.recv(1024)
        if response:
            print(f"✓ Получен ответ от сервера: {len(response)} байт")
            print(f"  Первый байт: 0x{response[0]:02x}")
            
            if len(response) >= 5:
                stream_id = struct.unpack('!I', response[1:5])[0]
                print(f"  ID стрима: {stream_id}")
                
                # Тестируем присоединение к стриму
                join_payload = struct.pack('!I', stream_id)
                join_message = b'\x05' + join_payload  # CLIENT_STREAM_JOIN
                sock.send(join_message)
                print("✓ Отправлен запрос на присоединение к стриму")
                
                # Ждем ответ на присоединение
                join_response = sock.recv(1024)
                if join_response:
                    print(f"✓ Получен ответ на присоединение: {len(join_response)} байт")
                else:
                    print("✗ Нет ответа на присоединение")
            else:
                print("✗ Неверный формат ответа (слишком короткий)")
        else:
            print("✗ Нет ответа от сервера")
            
    except Exception as e:
        print(f"✗ Ошибка: {e}")
    finally:
        sock.close()
        print("✓ Соединение закрыто")

def test_multiple_clients():
    """Тест с несколькими клиентами"""
    print("\n=== Тест нескольких клиентов ===")
    
    def client_thread(client_id):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(5)
            sock.connect(('localhost', 23231))
            
            # Отправляем UDP-адрес
            udp_port = 20000 + client_id
            udp_payload = struct.pack('!HHI8s', 2, udp_port, 0x7F000001, b'\x00' * 8)
            sock.send(b'\x01' + udp_payload)
            
            # Создаем стрим
            sock.send(b'\x03')
            
            # Получаем ответ
            response = sock.recv(1024)
            if response and len(response) >= 5:
                print(f"  Клиент {client_id}: создан стрим")
            else:
                print(f"  Клиент {client_id}: нет ответа")
                
            sock.close()
            
        except Exception as e:
            print(f"  Клиент {client_id}: ошибка - {e}")
    
    # Запускаем 3 клиента
    threads = []
    for i in range(3):
        t = threading.Thread(target=client_thread, args=(i,))
        threads.append(t)
        t.start()
    
    for t in threads:
        t.join()
    
    print("✓ Тест нескольких клиентов завершен")

def test_udp_communication():
    """Тест UDP-коммуникации"""
    print("\n=== Тест UDP ===")
    
    try:
        # Создаем UDP-сокет для тестирования
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.settimeout(2)
        
        # Пытаемся отправить тестовый UDP-пакет на сервер
        test_data = b'test_udp_packet'
        udp_sock.sendto(test_data, ('localhost', 23230))
        print("✓ Отправлен тестовый UDP-пакет")
        
        # Пытаемся получить ответ (сервер может не отвечать на неформатные пакеты)
        try:
            data, addr = udp_sock.recvfrom(1024)
            print(f"✓ Получен UDP-ответ: {len(data)} байт от {addr}")
        except socket.timeout:
            print("  (Нет UDP-ответа - это нормально для неформатных пакетов)")
            
    except Exception as e:
        print(f"✗ Ошибка UDP: {e}")
    finally:
        udp_sock.close()

def test_protocol_parsing():
    """Тест парсинга протокола"""
    print("\n=== Тест парсинга протокола ===")
    
    # Тестируем правильность форматов сообщений
    try:
        # StreamIDPayload
        stream_id = 12345
        payload = struct.pack('!I', stream_id)
        parsed_id = struct.unpack('!I', payload)[0]
        print(f"✓ StreamIDPayload: {stream_id} -> {parsed_id}")
        
        # UDPAddrFullPayload  
        family = 2  # AF_INET
        port = 54321
        ip = 0x7F000001  # 127.0.0.1
        zero = b'\x00' * 8
        udp_payload = struct.pack('!HHI8s', family, port, ip, zero)
        print(f"✓ UDPAddrFullPayload: {len(udp_payload)} байт")
        
    except Exception as e:
        print(f"✗ Ошибка парсинга: {e}")

if __name__ == "__main__":
    print("Тестовый клиент для проверки сервера видеоконференций")
    print("Убедитесь, что сервер запущен на localhost:23231 (TCP) и 23230 (UDP)\n")
    
    test_basic_communication()
    test_multiple_clients() 
    test_udp_communication()
    test_protocol_parsing()
    
    print("\n=== Тестирование завершено ===")