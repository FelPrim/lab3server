#ifndef UDPPACKET_H
#define UDPPACKET_H

#include <vector>
#include <cstdint>
#include <netinet/in.h>

// Структура для представления UDP пакета с информацией об отправителе
struct UdpPacket {
    std::vector<uint8_t> data;
    sockaddr_in fromAddress;
    time_t timestamp;
    
    UdpPacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress)
        : data(data), fromAddress(fromAddress) {
        timestamp = time(nullptr);
    }
};

// Базовый класс для обработчиков UDP пакетов
class UdpPacketHandler {
public:
    virtual ~UdpPacketHandler() = default;
    virtual void handlePacket(const UdpPacket& packet) = 0;
};

#endif
