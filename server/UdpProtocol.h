#ifndef UDPPROTOCOL_H
#define UDPPROTOCOL_H

#include <cstdint>

namespace UdpProtocol {
    // Типы UDP пакетов
    enum PacketType : uint32_t {
        VIDEO_PACKET = 1,      // Видео данные
        AUDIO_PACKET = 2,      // Аудио данные
        BROADCAST_PACKET = 3,  // Широковещательные данные
        CONTROL_PACKET = 4     // Контрольные сообщения
    };
    
    // Структура заголовка видео пакета
    struct VideoPacketHeader {
        uint32_t packetType;   // VIDEO_PACKET
        char conferenceId[32]; // ID конференции
        uint32_t sequence;     // Порядковый номер пакета
        uint32_t timestamp;    // Временная метка
        // После заголовка следуют видео данные
    };
    
    // Структура заголовка аудио пакета
    struct AudioPacketHeader {
        uint32_t packetType;   // AUDIO_PACKET
        char senderId[32];     // ID отправителя
        char recipientId[32];  // ID получателя (или пусто для широковещания)
        uint32_t sequence;     // Порядковый номер пакета
        uint32_t timestamp;    // Временная метка
        // После заголовка следуют аудио данные
    };
    
    // Структура широковещательного пакета
    struct BroadcastPacketHeader {
        uint32_t packetType;   // BROADCAST_PACKET
        char conferenceId[32]; // ID конференции
        uint32_t sequence;     // Порядковый номер пакета
        // После заголовка следуют данные
    };
}

#endif
