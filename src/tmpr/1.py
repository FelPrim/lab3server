#!/usr/bin/env python3
import socket
import struct
import sys

def debug_udp_server(port):
    """UDP сервер для отладки - показывает что приходит от сервера"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', port))
    
    print(f"🔍 UDP Debug Server listening on port {port}")
    print("Waiting for packets from your C server...")
    
    packet_count = 0
    
    try:
        while True:
            data, addr = sock.recvfrom(2048)
            packet_count += 1
            print(f"\n📦 Packet #{packet_count} from {addr}")
            print(f"   Length: {len(data)} bytes")
            
            if len(data) >= 8:
                try:
                    stream_id = struct.unpack('>I', data[0:4])[0]  # Big Endian
                    packet_num = struct.unpack('>I', data[4:8])[0]  # Big Endian
                    print(f"   Stream ID: {stream_id}")
                    print(f"   Packet Number: {packet_num}")
                    print(f"   Data size: {len(data) - 8} bytes")
                    
                    # Выводим первые 16 байт данных в hex
                    if len(data) > 8:
                        hex_data = data[8:min(24, len(data))].hex()
                        print(f"   Data (hex): {hex_data}")
                except Exception as e:
                    print(f"   Error parsing packet: {e}")
            else:
                print(f"   Short packet, raw data: {data.hex()}")
                
    except KeyboardInterrupt:
        print(f"\n🛑 Debug server stopped. Received {packet_count} packets total.")
    finally:
        sock.close()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python udp_debug_server.py <port>")
        print("Example: python udp_debug_server.py 50161")
        sys.exit(1)
    
    port = int(sys.argv[1])
    debug_udp_server(port)
