#!/usr/bin/env python3
import socket
import struct
import threading
import time
from datetime import datetime

class TestStreamServer:
    def __init__(self, host='localhost', port=23231):
        self.host = host
        self.port = port
        self.socket = None
        self.clients = []
        self.running = False
        
    def start(self):
        """Запуск сервера"""
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        
        try:
            self.socket.bind((self.host, self.port))
            self.socket.listen(5)
            self.running = True
            print(f"✅ Test server started on {self.host}:{self.port}")
            print("Commands:")
            print("  'create [id]' - send SERVER_STREAM_CREATED with given ID")
            print("  'start [id]'  - send SERVER_STREAM_START")
            print("  'end [id]'    - send SERVER_STREAM_END")
            print("  'delete [id]' - send SERVER_STREAM_DELETED")
            print("  'quit'        - stop server")
            
            # Запускаем поток для приема подключений
            accept_thread = threading.Thread(target=self.accept_connections)
            accept_thread.daemon = True
            accept_thread.start()
            
            # Запускаем поток для ввода команд
            input_thread = threading.Thread(target=self.command_input)
            input_thread.daemon = True
            input_thread.start()
            
            accept_thread.join()
            
        except Exception as e:
            print(f"❌ Failed to start server: {e}")
        finally:
            self.stop()
            
    def accept_connections(self):
        """Принимаем входящие подключения"""
        while self.running:
            try:
                client_socket, address = self.socket.accept()
                print(f"🔗 New connection from {address}")
                self.clients.append(client_socket)
                
                # Запускаем обработчик для клиента
                client_thread = threading.Thread(target=self.handle_client, args=(client_socket, address))
                client_thread.daemon = True
                client_thread.start()
                
            except Exception as e:
                if self.running:
                    print(f"❌ Accept error: {e}")
                    
    def handle_client(self, client_socket, address):
        """Обрабатываем сообщения от клиента"""
        try:
            while self.running:
                data = client_socket.recv(1024)
                if not data:
                    break
                    
                self.process_client_message(data, address, client_socket)
                
        except Exception as e:
            print(f"❌ Client {address} error: {e}")
        finally:
            if client_socket in self.clients:
                self.clients.remove(client_socket)
            client_socket.close()
            print(f"🔌 Connection closed: {address}")
            
    def process_client_message(self, data, address, client_socket):
        """Обрабатываем сообщение от клиента"""
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        
        if not data:
            return
            
        message_type = data[0]
        print(f"📨 [{timestamp}] Received from {address}: {len(data)} bytes")
        print(f"   First byte (type): 0x{message_type:02x}")
        
        # CLIENT_UDP_ADDR = 0x01
        if message_type == 0x01:
            print("   Type: CLIENT_UDP_ADDR")
            if len(data) >= 17:  # 1 byte type + 16 bytes sockaddr_in
                # Парсим sockaddr_in структуру
                family = struct.unpack('>H', data[1:3])[0]
                port = struct.unpack('>H', data[3:5])[0]
                ip = struct.unpack('>I', data[5:9])[0]
                ip_str = socket.inet_ntoa(data[5:9])
                print(f"   UDP Address: {ip_str}:{port}")
                
        # CLIENT_DISCONNECT = 0x02
        elif message_type == 0x02:
            print("   Type: CLIENT_DISCONNECT")
            
        # CLIENT_STREAM_CREATE = 0x03
        elif message_type == 0x03:
            print("   Type: CLIENT_STREAM_CREATE")
            print("   ⏳ Client is waiting for SERVER_STREAM_CREATED response...")
            
        # CLIENT_STREAM_DELETE = 0x04
        elif message_type == 0x04:
            if len(data) >= 5:
                stream_id = struct.unpack('>I', data[1:5])[0]
                print(f"   Type: CLIENT_STREAM_DELETE, Stream ID: {stream_id}")
            else:
                print("   Type: CLIENT_STREAM_DELETE (invalid format)")
                
        # CLIENT_STREAM_JOIN = 0x05
        elif message_type == 0x05:
            if len(data) >= 5:
                stream_id = struct.unpack('>I', data[1:5])[0]
                print(f"   Type: CLIENT_STREAM_JOIN, Stream ID: {stream_id}")
            else:
                print("   Type: CLIENT_STREAM_JOIN (invalid format)")
                
        # CLIENT_STREAM_LEAVE = 0x06
        elif message_type == 0x06:
            if len(data) >= 5:
                stream_id = struct.unpack('>I', data[1:5])[0]
                print(f"   Type: CLIENT_STREAM_LEAVE, Stream ID: {stream_id}")
            else:
                print("   Type: CLIENT_STREAM_LEAVE (invalid format)")
                
        else:
            print(f"   ⚠️  Unknown message type: 0x{message_type:02x}")
            
        # Показываем полные данные в hex
        hex_data = ' '.join(f'{b:02x}' for b in data)
        print(f"   Full data: {hex_data}")
        
    def send_server_message(self, message_type, stream_id=0):
        """Отправляем сообщение сервера всем подключенным клиентам"""
        if not self.clients:
            print("❌ No connected clients")
            return False
            
        # Формируем сообщение: 1 байт тип + 4 байта ID (big-endian)
        message = bytes([message_type]) + struct.pack('>I', stream_id)
        
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        type_names = {
            0x81: "SERVER_STREAM_CREATED",
            0x82: "SERVER_STREAM_DELETED", 
            0x83: "SERVER_STREAM_JOINED",
            0x84: "SERVER_STREAM_START",
            0x85: "SERVER_STREAM_END"
        }
        
        type_name = type_names.get(message_type, f"0x{message_type:02x}")
        print(f"📤 [{timestamp}] Sending {type_name} for stream {stream_id}")
        
        success_count = 0
        for client in self.clients[:]:  # Копируем список для безопасной итерации
            try:
                client.send(message)
                success_count += 1
            except Exception as e:
                print(f"❌ Failed to send to client: {e}")
                self.clients.remove(client)
                
        print(f"✅ Sent to {success_count} client(s)")
        return success_count > 0
        
    def command_input(self):
        """Обрабатываем ввод команд с консоли"""
        while self.running:
            try:
                command = input("\n🎮 Enter command: ").strip().lower()
                
                if command == 'quit':
                    print("🛑 Shutting down server...")
                    self.stop()
                    break
                    
                elif command.startswith('create'):
                    parts = command.split()
                    stream_id = int(parts[1]) if len(parts) > 1 else 123456
                    self.send_server_message(0x81, stream_id)
                    
                elif command.startswith('start'):
                    parts = command.split()
                    stream_id = int(parts[1]) if len(parts) > 1 else 123456
                    self.send_server_message(0x84, stream_id)
                    
                elif command.startswith('end'):
                    parts = command.split()
                    stream_id = int(parts[1]) if len(parts) > 1 else 123456
                    self.send_server_message(0x85, stream_id)
                    
                elif command.startswith('delete'):
                    parts = command.split()
                    stream_id = int(parts[1]) if len(parts) > 1 else 123456
                    self.send_server_message(0x82, stream_id)
                    
                elif command.startswith('joined'):
                    parts = command.split()
                    stream_id = int(parts[1]) if len(parts) > 1 else 123456
                    self.send_server_message(0x83, stream_id)
                    
                else:
                    print("❓ Unknown command. Available: create, start, end, delete, joined, quit")
                    
            except Exception as e:
                print(f"❌ Command error: {e}")
                
    def stop(self):
        """Останавливаем сервер"""
        self.running = False
        for client in self.clients:
            try:
                client.close()
            except:
                pass
        self.clients.clear()
        
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
        print("🛑 Server stopped")

if __name__ == "__main__":
    server = TestStreamServer()
    try:
        server.start()
    except KeyboardInterrupt:
        print("\n🛑 Server interrupted by user")
        server.stop()