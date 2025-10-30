#include "Message.h"
#include <stdexcept>
#include <cstring>

Message parseMessage(const std::vector<uint8_t>& data) {
    Message msg;
    if (!data.empty()) {
	msg.type = data[0];
	if (data.size() > 1) {
	    msg.data.assign(data.begin() + 1, data.end());
	}
    }
    return msg;
}

std::vector<uint8_t> serializeMessage(uint8_t type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result;
    result.push_back(type);
    result.insert(result.end(), data.begin(), data.end());
    return result;
}

// Вспомогательные функции для работы со строками
std::vector<uint8_t> stringToBytes(const std::string& str) {
    std::vector<uint8_t> result;
    uint16_t length = static_cast<uint16_t>(str.length());
    
    // Записываем длину строки (2 байта)
    result.push_back(static_cast<uint8_t>((length >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>(length & 0xFF));
    
    // Записываем саму строку
    result.insert(result.end(), str.begin(), str.end());
    
    return result;
}

std::string bytesToString(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 2 > data.size()) {
	throw std::runtime_error("Not enough data to read string length");
    }
    
    // Читаем длину строки
    uint16_t length = (static_cast<uint16_t>(data[offset]) << 8) | 
		      static_cast<uint16_t>(data[offset + 1]);
    offset += 2;
    
    if (offset + length > data.size()) {
	throw std::runtime_error("Not enough data to read string");
    }
    
    std::string result(data.begin() + offset, data.begin() + offset + length);
    offset += length;
    
    return result;
}

std::vector<uint8_t> stringsToBytes(const std::vector<std::string>& strings) {
    std::vector<uint8_t> result;
    
    // Записываем количество строк (2 байта)
    uint16_t count = static_cast<uint16_t>(strings.size());
    result.push_back(static_cast<uint8_t>((count >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>(count & 0xFF));
    
    // Записываем каждую строку
    for (const auto& str : strings) {
	auto strBytes = stringToBytes(str);
	result.insert(result.end(), strBytes.begin(), strBytes.end());
    }
    
    return result;
}

std::vector<std::string> bytesToStrings(const std::vector<uint8_t>& data, size_t& offset) {
    if (offset + 2 > data.size()) {
	throw std::runtime_error("Not enough data to read strings count");
    }
    
    // Читаем количество строк
    uint16_t count = (static_cast<uint16_t>(data[offset]) << 8) | 
		     static_cast<uint16_t>(data[offset + 1]);
    offset += 2;
    
    std::vector<std::string> result;
    for (uint16_t i = 0; i < count; ++i) {
	result.push_back(bytesToString(data, offset));
    }
    
    return result;
}

// Сериализация клиентских сообщений
std::vector<uint8_t> serializeClientUdpAddress(const ClientUdpAddressMessage& msg) {
    std::vector<uint8_t> result;
    
    // Сериализуем IP (4 байта)
    uint32_t ip = msg.address.ip.s_addr;
    result.push_back(static_cast<uint8_t>((ip >> 24) & 0xFF));
    result.push_back(static_cast<uint8_t>((ip >> 16) & 0xFF));
    result.push_back(static_cast<uint8_t>((ip >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>(ip & 0xFF));
    
    // Сериализуем порт (2 байта)
    result.push_back(static_cast<uint8_t>((msg.address.port >> 8) & 0xFF));
    result.push_back(static_cast<uint8_t>(msg.address.port & 0xFF));
    
    return result;
}

std::vector<uint8_t> serializeClientConference(const ClientConferenceMessage& msg) {
    return stringToBytes(msg.conferenceId);
}

std::vector<uint8_t> serializeClientVideoStream(const ClientVideoStreamMessage& msg) {
    std::vector<uint8_t> result;
    
    auto conferenceBytes = stringToBytes(msg.conferenceId);
    auto streamBytes = stringToBytes(msg.streamId);
    
    result.insert(result.end(), conferenceBytes.begin(), conferenceBytes.end());
    result.insert(result.end(), streamBytes.begin(), streamBytes.end());
    
    return result;
}

// Десериализация клиентских сообщений
ClientUdpAddressMessage deserializeClientUdpAddress(const std::vector<uint8_t>& data) {
    if (data.size() != 6) {
	throw std::runtime_error("Invalid UDP address message size");
    }
    
    ClientUdpAddressMessage msg;
    
    // Десериализуем IP
    uint32_t ip = (static_cast<uint32_t>(data[0]) << 24) |
		  (static_cast<uint32_t>(data[1]) << 16) |
		  (static_cast<uint32_t>(data[2]) << 8) |
		  static_cast<uint32_t>(data[3]);
    msg.address.ip.s_addr = ip;
    
    // Десериализуем порт
    msg.address.port = (static_cast<uint16_t>(data[4]) << 8) |
		       static_cast<uint16_t>(data[5]);
    
    return msg;
}

ClientConferenceMessage deserializeClientConference(const std::vector<uint8_t>& data) {
    ClientConferenceMessage msg;
    size_t offset = 0;
    msg.conferenceId = bytesToString(data, offset);
    return msg;
}

ClientVideoStreamMessage deserializeClientVideoStream(const std::vector<uint8_t>& data) {
    ClientVideoStreamMessage msg;
    size_t offset = 0;
    msg.conferenceId = bytesToString(data, offset);
    msg.streamId = bytesToString(data, offset);
    return msg;
}

// Сериализация серверных сообщений
std::vector<uint8_t> serializeServerConferenceCreated(const ServerConferenceCreatedMessage& msg) {
    return stringToBytes(msg.conferenceId);
}

std::vector<uint8_t> serializeServerConferenceClosed(const ServerConferenceClosedMessage& msg) {
    return stringToBytes(msg.conferenceId);
}

std::vector<uint8_t> serializeServerConferenceJoined(const ServerConferenceJoinedMessage& msg) {
    std::vector<uint8_t> result;
    
    // Сериализуем ID конференции
    auto conferenceBytes = stringToBytes(msg.conferenceId);
    result.insert(result.end(), conferenceBytes.begin(), conferenceBytes.end());
    
    // Сериализуем список участников
    auto participantsBytes = stringsToBytes(msg.participants);
    result.insert(result.end(), participantsBytes.begin(), participantsBytes.end());
    
    // Сериализуем список видеостримов
    auto streamsBytes = stringsToBytes(msg.videoStreams);
    result.insert(result.end(), streamsBytes.begin(), streamsBytes.end());
    
    return result;
}

std::vector<uint8_t> serializeServerParticipant(const ServerParticipantMessage& msg) {
    std::vector<uint8_t> result;
    
    auto conferenceBytes = stringToBytes(msg.conferenceId);
    auto participantBytes = stringToBytes(msg.participantId);
    
    result.insert(result.end(), conferenceBytes.begin(), conferenceBytes.end());
    result.insert(result.end(), participantBytes.begin(), participantBytes.end());
    
    return result;
}

std::vector<uint8_t> serializeServerVideoStream(const ServerVideoStreamMessage& msg) {
    std::vector<uint8_t> result;
    
    auto conferenceBytes = stringToBytes(msg.conferenceId);
    auto streamBytes = stringToBytes(msg.streamId);
    auto participantBytes = stringToBytes(msg.participantId);
    
    result.insert(result.end(), conferenceBytes.begin(), conferenceBytes.end());
    result.insert(result.end(), streamBytes.begin(), streamBytes.end());
    result.insert(result.end(), participantBytes.begin(), participantBytes.end());
    
    return result;
}

// Десериализация серверных сообщений
ServerConferenceCreatedMessage deserializeServerConferenceCreated(const std::vector<uint8_t>& data) {
    ServerConferenceCreatedMessage msg;
    size_t offset = 0;
    msg.conferenceId = bytesToString(data, offset);
    return msg;
}

ServerConferenceClosedMessage deserializeServerConferenceClosed(const std::vector<uint8_t>& data) {
    ServerConferenceClosedMessage msg;
    size_t offset = 0;
    msg.conferenceId = bytesToString(data, offset);
    return msg;
}

ServerConferenceJoinedMessage deserializeServerConferenceJoined(const std::vector<uint8_t>& data) {
    ServerConferenceJoinedMessage msg;
    size_t offset = 0;
    
    msg.conferenceId = bytesToString(data, offset);
    msg.participants = bytesToStrings(data, offset);
    msg.videoStreams = bytesToStrings(data, offset);
    
    return msg;
}

ServerParticipantMessage deserializeServerParticipant(const std::vector<uint8_t>& data) {
    ServerParticipantMessage msg;
    size_t offset = 0;
    msg.conferenceId = bytesToString(data, offset);
    msg.participantId = bytesToString(data, offset);
    return msg;
}

ServerVideoStreamMessage deserializeServerVideoStream(const std::vector<uint8_t>& data) {
    ServerVideoStreamMessage msg;
    size_t offset = 0;
    msg.conferenceId = bytesToString(data, offset);
    msg.streamId = bytesToString(data, offset);
    msg.participantId = bytesToString(data, offset);
    return msg;
}

/* Пример использования:
 *
 * 
 * // Клиент отправляет UDP адрес
ClientUdpAddressMessage udpMsg;
udpMsg.address = UdpAddress(clientAddress);
auto serialized = serializeClientUdpAddress(udpMsg);
auto message = serializeMessage(static_cast<uint8_t>(ClientMessageType::UDP_ADDRESS), serialized);

// Сервер принимает и парсит
Message received = parseMessage(message);
if (received.type == static_cast<uint8_t>(ClientMessageType::UDP_ADDRESS)) {
    ClientUdpAddressMessage parsed = deserializeClientUdpAddress(received.data);
    // Используем parsed.address
}
 * */
