#!/usr/bin/env python3
import socket
import struct
import sys

def test_udp_server(port):
    """Тестовый UDP сервер для проверки приема пакетов"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', port))
    
    print(f"🔍 UDP Test Server listening on port {port}")
    print("Waiting for packets...")
    
    try:
        while True:
            data, addr = sock.recvfrom(2048)
            print(f"📦 Received {len(data)} bytes from {addr}")
            
            if len(data) >= 8:
                try:
                    stream_id = struct.unpack('>I', data[0:4])[0]
                    packet_num = struct.unpack('>I', data[4:8])[0]
                    print(f"   Stream ID: {stream_id}, Packet: {packet_num}")
                except:
                    print(f"   Raw data (hex): {data.hex()}")
            else:
                print(f"   Short packet: {len(data)} bytes")
                
    except KeyboardInterrupt:
        print("\n🛑 Test server stopped")
    finally:
        sock.close()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python udp_test_server.py <port>")
        print("Example: python udp_test_server.py 50161")
        sys.exit(1)
    
    port = int(sys.argv[1])
    test_udp_server(port)
