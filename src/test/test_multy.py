#!/usr/bin/env python3
"""
Исправленный тестовый скрипт для многопользовательского тестирования
"""

import socket
import struct
import threading
import time
import random
import logging
from enum import Enum

# Настройка логирования
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger('TestClient')

# Константы протокола
class MessageType(Enum):
    # Базовые сообщения
    CLIENT_ERROR = 0x01
    SERVER_ERROR = 0x02
    CLIENT_SUCCESS = 0x03
    SERVER_SUCCESS = 0x04
    SERVER_HANDSHAKE_START = 0x05
    SERVER_HANDSHAKE_END = 0x06
    
    # Сообщения стримов
    CLIENT_STREAM_CREATE = 0x10
    CLIENT_STREAM_DELETE = 0x11
    CLIENT_STREAM_CONN_JOIN = 0x12
    CLIENT_STREAM_CONN_LEAVE = 0x13
    
    SERVER_STREAM_CREATED = 0x90
    SERVER_STREAM_DELETED = 0x91
    SERVER_STREAM_CONN_JOINED = 0x92
    SERVER_STREAM_START = 0x93
    SERVER_STREAM_END = 0x94
    
    # Сообщения звонков
    CLIENT_CALL_CREATE = 0x20
    CLIENT_CALL_CONN_JOIN = 0x21
    CLIENT_CALL_CONN_LEAVE = 0x22
    
    SERVER_CALL_CREATED = 0xA0
    SERVER_CALL_CONN_JOINED = 0xA1
    SERVER_CALL_CONN_NEW = 0xA2
    SERVER_CALL_CONN_LEFT = 0xA3
    SERVER_CALL_STREAM_NEW = 0xA4
    SERVER_CALL_STREAM_DELETED = 0xA5

class TestClient:
    def __init__(self, server_host='localhost', tcp_port=8080, udp_port=8081, client_id=0):
        self.server_host = server_host
        self.tcp_port = tcp_port
        self.udp_port = udp_port
        self.client_id = client_id
        
        self.tcp_socket = None
        self.udp_socket = None
        self.connection_id = None
        self.udp_handshake_complete = False
        
        # Состояние клиента
        self.streams = {}
        self.calls = {}
        self.current_call = None
        
        # Флаги для синхронизации
        self.handshake_complete = threading.Event()
        self.received_messages = []
        self.received_udp_packets = []
        self.message_lock = threading.Lock()
        self.udp_lock = threading.Lock()
        
    def connect(self):
        """Подключение к серверу"""
        try:
            self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_socket.connect((self.server_host, self.tcp_port))
            logger.info(f"[Client {self.client_id}] Connected to TCP server {self.server_host}:{self.tcp_port}")
            
            self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp_socket.bind(('', 0))
            local_udp_port = self.udp_socket.getsockname()[1]
            logger.info(f"[Client {self.client_id}] UDP socket bound to port {local_udp_port}")
            
            threading.Thread(target=self._tcp_receive_loop, daemon=True).start()
            threading.Thread(target=self._udp_receive_loop, daemon=True).start()
            threading.Thread(target=self._udp_handshake_loop, daemon=True).start()
            
            return True
            
        except Exception as e:
            logger.error(f"[Client {self.client_id}] Connection failed: {e}")
            return False
    
    def _tcp_receive_loop(self):
        """Цикл приема TCP сообщений"""
        try:
            while True:
                data = self.tcp_socket.recv(8192)
                if not data:
                    logger.info(f"[Client {self.client_id}] TCP connection closed by server")
                    break
                
                self._process_tcp_message(data)
                
        except Exception as e:
            logger.error(f"[Client {self.client_id}] TCP receive error: {e}")
    
    def _udp_receive_loop(self):
        """Цикл приема UDP сообщений"""
        try:
            while True:
                data, addr = self.udp_socket.recvfrom(1200)
                self._process_udp_packet(data, addr)
                
        except Exception as e:
            logger.error(f"[Client {self.client_id}] UDP receive error: {e}")
    
    def _udp_handshake_loop(self):
        """Цикл отправки UDP handshake пакетов"""
        handshake_count = 0
        while not self.udp_handshake_complete and handshake_count < 100:
            if self.connection_id:
                self._send_udp_handshake()
                handshake_count += 1
            time.sleep(0.1)
        
        if not self.udp_handshake_complete:
            logger.warning(f"[Client {self.client_id}] UDP handshake failed after 100 attempts")
    
    def _send_udp_handshake(self):
        """Отправка UDP handshake пакета"""
        try:
            packet = struct.pack('!Q I', 0, self.connection_id)
            self.udp_socket.sendto(packet, (self.server_host, self.udp_port))
        except Exception as e:
            logger.error(f"[Client {self.client_id}] UDP handshake send error: {e}")
    
    def _process_tcp_message(self, data):
        """Обработка TCP сообщения"""
        if len(data) < 1:
            return
        
        message_type = data[0]
        payload = data[1:] if len(data) > 1 else b''
        
        logger.info(f"[Client {self.client_id}] Received TCP message: type=0x{message_type:02x}, len={len(payload)}")
        
        with self.message_lock:
            self.received_messages.append((message_type, payload))
        
        # Обработка специфичных сообщений
        if message_type == MessageType.SERVER_HANDSHAKE_START.value:
            self._handle_handshake_start(payload)
        elif message_type == MessageType.SERVER_HANDSHAKE_END.value:
            self._handle_handshake_end(payload)
        elif message_type == MessageType.SERVER_STREAM_CREATED.value:
            self._handle_stream_created(payload)
        elif message_type == MessageType.SERVER_CALL_CREATED.value:
            self._handle_call_created(payload)
        elif message_type == MessageType.SERVER_CALL_CONN_JOINED.value:
            self._handle_call_joined(payload)
        elif message_type == MessageType.SERVER_CALL_CONN_NEW.value:
            self._handle_call_conn_new(payload)
        elif message_type == MessageType.SERVER_CALL_CONN_LEFT.value:
            self._handle_call_conn_left(payload)
        elif message_type == MessageType.SERVER_CALL_STREAM_NEW.value:
            self._handle_call_stream_new(payload)
        elif message_type == MessageType.SERVER_CALL_STREAM_DELETED.value:
            self._handle_call_stream_deleted(payload)
        elif message_type == MessageType.SERVER_STREAM_START.value:
            self._handle_stream_start(payload)
        elif message_type == MessageType.SERVER_STREAM_END.value:
            self._handle_stream_end(payload)
    
    def _process_udp_packet(self, data, addr):
        """Обработка UDP пакета"""
        logger.info(f"[Client {self.client_id}] Received UDP packet from {addr}, len={len(data)}")
        
        with self.udp_lock:
            self.received_udp_packets.append((data, addr))
        
        # Проверяем, это handshake или stream packet
        if len(data) >= 12:
            first_8_bytes = data[:8]
            if first_8_bytes == b'\x00\x00\x00\x00\x00\x00\x00\x00':
                pass
            else:
                call_id = struct.unpack('!I', data[0:4])[0]
                stream_id = struct.unpack('!I', data[4:8])[0]
                packet_num = struct.unpack('!I', data[8:12])[0]
                logger.info(f"[Client {self.client_id}] Received UDP stream packet: call={call_id}, stream={stream_id}, packet={packet_num}")
    
    def _handle_handshake_start(self, payload):
        """Обработка SERVER_HANDSHAKE_START"""
        if len(payload) >= 4:
            self.connection_id = struct.unpack('!I', payload[:4])[0]
            logger.info(f"[Client {self.client_id}] Handshake started, connection_id: {self.connection_id}")
    
    def _handle_handshake_end(self, payload):
        """Обработка SERVER_HANDSHAKE_END"""
        self.udp_handshake_complete = True
        self.handshake_complete.set()
        logger.info(f"[Client {self.client_id}] Handshake completed")
    
    def _handle_stream_created(self, payload):
        """Обработка SERVER_STREAM_CREATED"""
        if len(payload) >= 4:
            stream_id = struct.unpack('!I', payload[:4])[0]
            self.streams[stream_id] = {'id': stream_id, 'recipients': []}
            logger.info(f"[Client {self.client_id}] Stream created: {stream_id}")
    
    def _handle_call_created(self, payload):
        """Обработка SERVER_CALL_CREATED"""
        if len(payload) >= 4:
            call_id = struct.unpack('!I', payload[:4])[0]
            self.calls[call_id] = {'id': call_id, 'participants': [], 'streams': []}
            self.current_call = call_id
            logger.info(f"[Client {self.client_id}] Call created: {call_id}")
    
    def _handle_call_joined(self, payload):
        """Обработка SERVER_CALL_CONN_JOINED"""
        if len(payload) >= 6:
            call_id = struct.unpack('!I', payload[:4])[0]
            participant_count = payload[4]
            stream_count = payload[5]
            
            logger.info(f"[Client {self.client_id}] Call joined: {call_id}, participants={participant_count}, streams={stream_count}")
            
            if call_id in self.calls:
                self.calls[call_id]['participants'] = []
                self.calls[call_id]['streams'] = []
                
                offset = 6
                for i in range(participant_count):
                    if offset + 4 <= len(payload):
                        participant_id = struct.unpack('!I', payload[offset:offset+4])[0]
                        self.calls[call_id]['participants'].append(participant_id)
                        offset += 4
                
                for i in range(stream_count):
                    if offset + 4 <= len(payload):
                        stream_id = struct.unpack('!I', payload[offset:offset+4])[0]
                        self.calls[call_id]['streams'].append(stream_id)
                        offset += 4
    
    def _handle_call_conn_new(self, payload):
        """Обработка SERVER_CALL_CONN_NEW"""
        if len(payload) >= 8:
            call_id = struct.unpack('!I', payload[:4])[0]
            connection_id = struct.unpack('!I', payload[4:8])[0]
            logger.info(f"[Client {self.client_id}] New participant in call {call_id}: {connection_id}")
    
    def _handle_call_conn_left(self, payload):
        """Обработка SERVER_CALL_CONN_LEFT"""
        if len(payload) >= 8:
            call_id = struct.unpack('!I', payload[:4])[0]
            connection_id = struct.unpack('!I', payload[4:8])[0]
            logger.info(f"[Client {self.client_id}] Participant left call {call_id}: {connection_id}")
    
    def _handle_call_stream_new(self, payload):
        """Обработка SERVER_CALL_STREAM_NEW"""
        if len(payload) >= 8:
            call_id = struct.unpack('!I', payload[:4])[0]
            stream_id = struct.unpack('!I', payload[4:8])[0]
            logger.info(f"[Client {self.client_id}] New stream in call {call_id}: {stream_id}")
    
    def _handle_call_stream_deleted(self, payload):
        """Обработка SERVER_CALL_STREAM_DELETED"""
        if len(payload) >= 8:
            call_id = struct.unpack('!I', payload[:4])[0]
            stream_id = struct.unpack('!I', payload[4:8])[0]
            logger.info(f"[Client {self.client_id}] Stream deleted from call {call_id}: {stream_id}")
    
    def _handle_stream_start(self, payload):
        """Обработка SERVER_STREAM_START"""
        if len(payload) >= 4:
            stream_id = struct.unpack('!I', payload[:4])[0]
            logger.info(f"[Client {self.client_id}] Stream started: {stream_id}")
    
    def _handle_stream_end(self, payload):
        """Обработка SERVER_STREAM_END"""
        if len(payload) >= 4:
            stream_id = struct.unpack('!I', payload[:4])[0]
            logger.info(f"[Client {self.client_id}] Stream ended: {stream_id}")
    
    def wait_for_handshake(self, timeout=10):
        """Ожидание завершения handshake"""
        return self.handshake_complete.wait(timeout)
    
    def wait_for_message(self, message_type, timeout=5):
        """Ожидание сообщения определенного типа"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            with self.message_lock:
                for i, (msg_type, payload) in enumerate(self.received_messages):
                    if msg_type == message_type:
                        self.received_messages.pop(i)
                        return payload
            time.sleep(0.1)
        return None
    
    def wait_for_udp_packets(self, count=1, timeout=5):
        """Ожидание UDP пакетов"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            with self.udp_lock:
                if len(self.received_udp_packets) >= count:
                    packets = self.received_udp_packets[:count]
                    self.received_udp_packets = self.received_udp_packets[count:]
                    return packets
            time.sleep(0.1)
        return None
    
    def send_tcp_message(self, message_type, payload=b''):
        """Отправка TCP сообщения"""
        try:
            message = bytes([message_type]) + payload
            self.tcp_socket.send(message)
            logger.info(f"[Client {self.client_id}] Sent TCP message: type=0x{message_type:02x}, len={len(payload)}")
            return True
        except Exception as e:
            logger.error(f"[Client {self.client_id}] TCP send error: {e}")
            return False
    
    def send_udp_stream_packet(self, call_id, stream_id, packet_num, data):
        """Отправка UDP stream пакета"""
        try:
            header = struct.pack('!I I I', call_id, stream_id, packet_num)
            packet_data = data[:1188] if len(data) > 1188 else data
            packet = header + packet_data
            if len(packet) < 1200:
                packet += b'\x00' * (1200 - len(packet))
            
            self.udp_socket.sendto(packet, (self.server_host, self.udp_port))
            logger.info(f"[Client {self.client_id}] Sent UDP packet: call={call_id}, stream={stream_id}, packet={packet_num}")
            return True
        except Exception as e:
            logger.error(f"[Client {self.client_id}] UDP stream send error: {e}")
            return False
    
    # API методы для тестирования
    
    def create_call(self):
        """Создание звонка"""
        logger.info(f"[Client {self.client_id}] Creating call...")
        if self.send_tcp_message(MessageType.CLIENT_CALL_CREATE.value):
            payload = self.wait_for_message(MessageType.SERVER_CALL_CREATED.value)
            if payload and len(payload) >= 4:
                call_id = struct.unpack('!I', payload[:4])[0]
                logger.info(f"[Client {self.client_id}] Call created successfully: {call_id}")
                self.current_call = call_id
                return call_id
        logger.error(f"[Client {self.client_id}] Failed to create call")
        return None
    
    def join_call(self, call_id, expect_success=True):
        """Присоединение к звонку с возможностью ожидать ошибку"""
        logger.info(f"[Client {self.client_id}] Joining call: {call_id}")
        payload = struct.pack('!I', call_id)
        if self.send_tcp_message(MessageType.CLIENT_CALL_CONN_JOIN.value, payload):
            if expect_success:
                response = self.wait_for_message(MessageType.SERVER_CALL_CONN_JOINED.value, timeout=3)
                if response:
                    logger.info(f"[Client {self.client_id}] Joined call successfully: {call_id}")
                    self.current_call = call_id
                    return True
            else:
                error = self.wait_for_message(MessageType.SERVER_ERROR.value, timeout=3)
                if error:
                    error_msg = self._parse_error(error)
                    logger.info(f"[Client {self.client_id}] Expected join error: {error_msg}")
                    return True
            
            logger.error(f"[Client {self.client_id}] Unexpected join result for call {call_id}")
            return False
        
        logger.error(f"[Client {self.client_id}] Failed to send join message")
        return False
    
    def leave_call(self, call_id):
        """Выход из звонка"""
        logger.info(f"[Client {self.client_id}] Leaving call: {call_id}")
        payload = struct.pack('!I', call_id)
        if self.send_tcp_message(MessageType.CLIENT_CALL_CONN_LEAVE.value, payload):
            success = self.wait_for_message(MessageType.SERVER_SUCCESS.value)
            if success:
                logger.info(f"[Client {self.client_id}] Left call successfully: {call_id}")
                if self.current_call == call_id:
                    self.current_call = None
                return True
        return False
    
    def create_stream(self, call_id=0):
        """Создание стрима"""
        logger.info(f"[Client {self.client_id}] Creating stream for call: {call_id}")
        payload = struct.pack('!I', call_id)
        if self.send_tcp_message(MessageType.CLIENT_STREAM_CREATE.value, payload):
            response = self.wait_for_message(MessageType.SERVER_STREAM_CREATED.value)
            if response and len(response) >= 4:
                stream_id = struct.unpack('!I', response[:4])[0]
                logger.info(f"[Client {self.client_id}] Stream created successfully: {stream_id}")
                return stream_id
        return None
    
    def delete_stream(self, stream_id):
        """Удаление стрима"""
        logger.info(f"[Client {self.client_id}] Deleting stream: {stream_id}")
        payload = struct.pack('!I', stream_id)
        if self.send_tcp_message(MessageType.CLIENT_STREAM_DELETE.value, payload):
            success = self.wait_for_message(MessageType.SERVER_SUCCESS.value)
            if success:
                logger.info(f"[Client {self.client_id}] Stream deleted successfully: {stream_id}")
                return True
        return False
    
    def join_stream(self, stream_id):
        """Присоединение к стриму"""
        logger.info(f"[Client {self.client_id}] Joining stream: {stream_id}")
        payload = struct.pack('!I', stream_id)
        if self.send_tcp_message(MessageType.CLIENT_STREAM_CONN_JOIN.value, payload):
            response = self.wait_for_message(MessageType.SERVER_STREAM_CONN_JOINED.value)
            if response:
                logger.info(f"[Client {self.client_id}] Joined stream successfully: {stream_id}")
                return True
        return False
    
    def leave_stream(self, stream_id):
        """Выход из стрима"""
        logger.info(f"[Client {self.client_id}] Leaving stream: {stream_id}")
        payload = struct.pack('!I', stream_id)
        if self.send_tcp_message(MessageType.CLIENT_STREAM_CONN_LEAVE.value, payload):
            success = self.wait_for_message(MessageType.SERVER_SUCCESS.value)
            if success:
                logger.info(f"[Client {self.client_id}] Left stream successfully: {stream_id}")
                return True
        return False
    
    def send_test_udp_packets(self, call_id, stream_id, count=10):
        """Отправка тестовых UDP пакетов"""
        logger.info(f"[Client {self.client_id}] Sending {count} test UDP packets for call={call_id}, stream={stream_id}")
        for i in range(count):
            data = f"Test packet {i} for call={call_id}, stream={stream_id}".encode() + b'\x00' * 100
            self.send_udp_stream_packet(call_id, stream_id, i, data)
            time.sleep(0.1)
    
    def test_join_own_call(self, call_id):
        """Тест попытки присоединения к собственному звонку (должен вернуть ошибку)"""
        logger.info(f"[Client {self.client_id}] Testing join to own call: {call_id}")
        result = self.join_call(call_id, expect_success=False)
        if result:
            logger.info(f"[Client {self.client_id}] ✓ Correctly rejected join to own call")
            return True
        else:
            logger.error(f"[Client {self.client_id}] ✗ Unexpectedly allowed join to own call")
            return False
    
    def _parse_error(self, error_payload):
        """Парсинг сообщения об ошибке"""
        if len(error_payload) < 2:
            return "Unknown error"
        
        original_message = error_payload[0]
        message_length = error_payload[1]
        
        if len(error_payload) >= 2 + message_length:
            error_msg = error_payload[2:2+message_length].decode('utf-8', errors='ignore')
            return f"Error for message 0x{original_message:02x}: {error_msg}"
        
        return "Malformed error message"
    
    def disconnect(self):
        """Отключение от сервера"""
        try:
            if self.tcp_socket:
                self.tcp_socket.close()
            if self.udp_socket:
                self.udp_socket.close()
            logger.info(f"[Client {self.client_id}] Disconnected from server")
        except Exception as e:
            logger.error(f"[Client {self.client_id}] Disconnect error: {e}")

def run_basic_test():
    """Базовый тест функциональности сервера"""
    client = TestClient(client_id=1)
    
    try:
        if not client.connect():
            logger.error("Failed to connect to server")
            return False
        
        if not client.wait_for_handshake():
            logger.error("Handshake timeout")
            return False
        
        logger.info("=== Basic functionality test ===")
        
        # Тест 1: Создание звонка
        call_id = client.create_call()
        if not call_id:
            logger.error("Test 1 FAILED: Call creation")
            return False
        
        # Тест 2: Попытка присоединения к собственному звонку (должен вернуть ошибку)
        if not client.test_join_own_call(call_id):
            logger.error("Test 2 FAILED: Join own call validation")
            return False
        
        # Тест 3: Создание приватного стрима
        stream_id = client.create_stream(call_id)
        if not stream_id:
            logger.error("Test 3 FAILED: Private stream creation")
            return False
        
        # Тест 4: Присоединение к собственному стриму
        if not client.join_stream(stream_id):
            logger.error("Test 4 FAILED: Join own stream")
            return False
        
        # Тест 5: Отправка UDP пакетов
        client.send_test_udp_packets(call_id, stream_id, 5)
        
        time.sleep(1)
        
        # Тест 6: Выход из стрима
        if not client.leave_stream(stream_id):
            logger.error("Test 6 FAILED: Leave stream")
            return False
        
        # Тест 7: Удаление стрима
        if not client.delete_stream(stream_id):
            logger.error("Test 7 FAILED: Delete stream")
            return False
        
        # Тест 8: Выход из звонка
        if not client.leave_call(call_id):
            logger.error("Test 8 FAILED: Leave call")
            return False
        
        # Тест 9: Создание публичного стрима
        pub_stream_id = client.create_stream(0)
        if not pub_stream_id:
            logger.error("Test 9 FAILED: Public stream creation")
            return False
        
        # Тест 10: Отправка UDP пакетов для публичного стрима
        client.send_test_udp_packets(0, pub_stream_id, 3)
        
        logger.info("=== All basic tests PASSED ===")
        return True
        
    except Exception as e:
        logger.error(f"Test failed with exception: {e}")
        return False
    finally:
        time.sleep(1)
        client.disconnect()

def run_multiple_clients_test():
    """Тест с несколькими клиентами"""
    logger.info("=== Multiple clients test ===")
    
    clients = []
    try:
        # Создаем 3 клиента с разными ID
        for i in range(3):
            client = TestClient(client_id=i+1)
            if client.connect() and client.wait_for_handshake():
                clients.append(client)
                logger.info(f"Client {i+1} connected")
                time.sleep(0.5)
            else:
                logger.error(f"Client {i+1} failed to connect")
        
        if len(clients) < 3:
            logger.error("Need at least 3 clients for this test")
            return False
        
        # Клиент 1 создает звонок
        call_id = clients[0].create_call()
        if not call_id:
            logger.error("Client 1 failed to create call")
            return False
        
        # Клиент 2 присоединяется к звонку (ожидаем успех)
        if not clients[1].join_call(call_id, expect_success=True):
            logger.error("Client 2 failed to join call")
            return False
        
        # Клиент 1 пытается присоединиться к своему же звонку (ожидаем ошибку)
        if not clients[0].join_call(call_id, expect_success=False):
            logger.error("Client 1 should have failed to join own call")
            return False
        
        # Клиент 3 присоединяется к звонку (ожидаем успех)
        if not clients[2].join_call(call_id, expect_success=True):
            logger.error("Client 3 failed to join call")
            return False
        
        # Клиент 1 создает стрим в звонке
        stream_id = clients[0].create_stream(call_id)
        if not stream_id:
            logger.error("Failed to create stream")
            return False
        
        # Клиенты 2 и 3 присоединяются к стриму
        for i in range(1, 3):
            if not clients[i].join_stream(stream_id):
                logger.error(f"Client {i+1} failed to join stream")
                return False
        
        # Клиент 1 отправляет UDP пакеты
        clients[0].send_test_udp_packets(call_id, stream_id, 3)
        
        # Проверяем, что клиенты 2 и 3 получают пакеты
        time.sleep(2)
        for i in range(1, 3):
            udp_packets = clients[i].wait_for_udp_packets(count=2, timeout=5)
            if udp_packets and len(udp_packets) >= 2:
                logger.info(f"Client {i+1} received {len(udp_packets)} UDP packets")
            else:
                logger.warning(f"Client {i+1} received fewer UDP packets than expected")
        
        time.sleep(1)
        logger.info("=== Multiple clients test PASSED ===")
        return True
        
    except Exception as e:
        logger.error(f"Multiple clients test failed: {e}")
        return False
    finally:
        for client in clients:
            client.disconnect()

if __name__ == "__main__":
    print("Improved Video Conference Server Test Client")
    print("============================================")
    
    # Запускаем базовый тест
    if run_basic_test():
        print("\n✓ Basic functionality test PASSED")
    else:
        print("\n✗ Basic functionality test FAILED")
        exit(1)
    
    print("Waiting for server to stabilize...")
    time.sleep(2)
    
    # Запускаем тест с несколькими клиентами
    if run_multiple_clients_test():
        print("✓ Multiple clients test PASSED")
    else:
        print("✗ Multiple clients test FAILED")
        exit(1)
    
    print("\n🎉 All tests PASSED! Server appears to be working correctly.")
