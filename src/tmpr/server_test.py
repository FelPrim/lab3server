#!/usr/bin/env python3
import socket
import struct
import sys
import time
from threading import Thread

# Команды протокола
CLIENT_UDP_ADDR = 0x01
CLIENT_STREAM_JOIN = 0x05
SERVER_STREAM_JOINED = 0x83

class TestClient:
    def __init__(self, server_host='marrs73.ru', tcp_port=23231, udp_port=23230):
        self.server_host = server_host
        self.tcp_port = tcp_port
        self.udp_port = udp_port
        self.tcp_socket = None
        self.udp_socket = None
        self.local_udp_port = 0
        self.stream_id = None
        self.running = True
        
    def start_udp_listener(self):
        """Запускает UDP слушатель в отдельном потоке"""
        def udp_listener():
            print(f"🟢 UDP listener started on port {self.local_udp_port}")
            while self.running:
                try:
                    self.udp_socket.settimeout(1.0)
                    data, addr = self.udp_socket.recvfrom(2048)
                    if len(data) >= 8:
                        stream_id = struct.unpack('>I', data[0:4])[0]
                        packet_num = struct.unpack('>I', data[4:8])[0]
                        print(f"📦 UDP packet received: stream_id={stream_id}, packet_number={packet_num}, len={len(data)}, from={addr}")
                    else:
                        print(f"📦 UDP packet received: len={len(data)}, from={addr}")
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.running:
                        print(f"❌ UDP receive error: {e}")
                    break
        
        Thread(target=udp_listener, daemon=True).start()
    
    def connect(self, stream_id):
        """Подключается к серверу и присоединяется к трансляции"""
        self.stream_id = stream_id
        
        try:
            # Создаем UDP сокет для приема
            self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp_socket.bind(('0.0.0.0', 0))
            self.local_udp_port = self.udp_socket.getsockname()[1]
            
            # ОТПРАВЛЯЕМ ПРОБНЫЙ UDP-ПАКЕТ ДЛЯ "ОТКРЫТИЯ" NAT
            try:
                probe_data = b'UDP_PROBE'
                self.udp_socket.sendto(probe_data, (self.server_host, self.udp_port))
                print(f"📤 Sent UDP probe to {self.server_host}:{self.udp_port}")
            except Exception as e:
                print(f"⚠️  UDP probe failed: {e}")
            
            # Запускаем слушатель UDP
            self.start_udp_listener()
            
            # Подключаемся по TCP
            self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_socket.connect((self.server_host, self.tcp_port))
            print(f"🟢 TCP connected to {self.server_host}:{self.tcp_port}")
            
            # Отправляем UDP адрес с IP 0.0.0.0
            family = 2  # AF_INET
            port = self.local_udp_port
            ip_bytes = socket.inet_aton('0.0.0.0')  # Ключевое изменение!
            
            udp_payload = struct.pack('>HH4s8s', 
                                    family,
                                    port,  
                                    ip_bytes,
                                    b'\x00' * 8)
            
            message = bytes([CLIENT_UDP_ADDR]) + udp_payload
            self.tcp_socket.send(message)
            print(f"📤 Sent UDP address: port={self.local_udp_port}, IP=0.0.0.0 (let server detect)")
            
            time.sleep(1)
            
            # Присоединяемся к трансляции
            stream_id_be = struct.pack('>I', stream_id)
            message = bytes([CLIENT_STREAM_JOIN]) + stream_id_be
            self.tcp_socket.send(message)
            print(f"📤 Sent JOIN request for stream {stream_id}")
            
            # Ждем ответ от сервера
            response = self.tcp_socket.recv(5)
            if response:
                msg_type = response[0]
                if msg_type == SERVER_STREAM_JOINED:
                    if len(response) == 5:
                        resp_stream_id = struct.unpack('>I', response[1:5])[0]
                        print(f"✅ Successfully joined stream {resp_stream_id}")
                    else:
                        print("❌ Join failed (error response)")
                else:
                    print(f"❌ Unexpected response type: 0x{msg_type:02x}")
            
            def tcp_listener():
                while self.running:
                    try:
                        data = self.tcp_socket.recv(1024)
                        if not data:
                            print("🔌 TCP connection closed by server")
                            break
                        print(f"📨 TCP message: {len(data)} bytes, type: 0x{data[0]:02x}")
                    except Exception as e:
                        if self.running:
                            print(f"❌ TCP receive error: {e}")
                        break
            
            Thread(target=tcp_listener, daemon=True).start()
            
            print("🎯 Client is ready. Waiting for UDP packets...")
            print("Press Ctrl+C to stop...")
            
            while self.running:
                time.sleep(1)
                
        except KeyboardInterrupt:
            print("\n🛑 Stopping client...")
        except Exception as e:
            print(f"❌ Connection error: {e}")
        finally:
            self.running = False
            if self.tcp_socket:
                self.tcp_socket.close()
            if self.udp_socket:
                self.udp_socket.close()
            print("🔌 All connections closed")

def main():
    if len(sys.argv) < 2:
        print("Usage: python server_test.py <stream_id> [server_host]")
        print("Examples:")
        print("  python server_test.py 95082631")
        print("  python server_test.py 95082631 marrs73.ru")
        sys.exit(1)
    
    try:
        stream_id = int(sys.argv[1])
        server_host = sys.argv[2] if len(sys.argv) > 2 else 'marrs73.ru'
    except ValueError:
        print("Error: stream_id must be an integer")
        sys.exit(1)
    
    print(f"🔧 Connecting to server: {server_host}")
    client = TestClient(server_host=server_host)
    client.connect(stream_id)

if __name__ == "__main__":
    main()
