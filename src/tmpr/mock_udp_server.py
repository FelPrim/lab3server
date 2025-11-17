#!/usr/bin/env python3
import socket
import sys

HOST = '0.0.0.0'
PORT = 23230 if len(sys.argv) < 2 else int(sys.argv[1])

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((HOST, PORT))
    print("UDP mock server listening on {}:{}".format(HOST, PORT))
    try:
        while True:
            data, addr = s.recvfrom(2048)
            print("Received UDP {} bytes from {}: {} (hex {})".format(len(data), addr, data, data.hex()))
    except KeyboardInterrupt:
        print("Stopping UDP server")
    s.close()

if __name__ == '__main__':
    main()
