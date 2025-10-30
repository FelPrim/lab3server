#ifndef MESSAGE_H
#define MESSAGE_H

#include <vector>
#include <cstdint>
#include <string>
#include <netinet/in.h>

// Типы сообщений от клиента к серверу
enum class ClientMessageType : uint8_t {
    UDP_ADDRESS = 1,
    DISCONNECT = 2,
    CREATE_CONFERENCE = 3,
    END_CONFERENCE = 4,
    JOIN_CONFERENCE = 5,
    LEAVE_CONFERENCE = 6,
    ADD_VIDEO_STREAM = 7,
    REMOVE_VIDEO_STREAM = 8
};

// Типы сообщений от сервера к клиенту
enum class ServerMessageType : uint8_t {
    CONFERENCE_CREATED = 1,
    CONFERENCE_CLOSED = 2,
    CONFERENCE_JOINED = 3,
    NEW_PARTICIPANT = 4,
    PARTICIPANT_LEFT = 5,
    VIDEO_STREAM_ADDED = 6,
    VIDEO_STREAM_REMOVED = 7
};

// Структура для представления UDP адреса
struct UdpAddress {
    in_addr ip;
    in_port_t port;
    
    UdpAddress() : ip({0}), port(0) {}
    UdpAddress(const sockaddr_in& addr) : ip(addr.sin_addr), port(addr.sin_port) {}
    
    sockaddr_in toSockaddr() const {
        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr = ip;
        addr.sin_port = port;
        return addr;
    }
};

// Базовые структуры сообщений
struct Message {
    uint8_t type;
    std::vector<uint8_t> data;
};

// Структуры для клиентских сообщений
struct ClientUdpAddressMessage {
    UdpAddress address;
};

struct ClientConferenceMessage {
    std::string conferenceId;
};

struct ClientVideoStreamMessage {
    std::string conferenceId;
    std::string streamId;
};

// Структуры для серверных сообщений
struct ServerConferenceCreatedMessage {
    std::string conferenceId;
};

struct ServerConferenceClosedMessage {
    std::string conferenceId;
};

struct ServerConferenceJoinedMessage {
    std::string conferenceId;
    std::vector<std::string> participants;
    std::vector<std::string> videoStreams;
};

struct ServerParticipantMessage {
    std::string conferenceId;
    std::string participantId;
};

struct ServerVideoStreamMessage {
    std::string conferenceId;
    std::string streamId;
    std::string participantId;
};

// Функции для сериализации/десериализации
Message parseMessage(const std::vector<uint8_t>& data);
std::vector<uint8_t> serializeMessage(uint8_t type, const std::vector<uint8_t>& data = {});

// Сериализация клиентских сообщений
std::vector<uint8_t> serializeClientUdpAddress(const ClientUdpAddressMessage& msg);
std::vector<uint8_t> serializeClientConference(const ClientConferenceMessage& msg);
std::vector<uint8_t> serializeClientVideoStream(const ClientVideoStreamMessage& msg);

// Десериализация клиентских сообщений
ClientUdpAddressMessage deserializeClientUdpAddress(const std::vector<uint8_t>& data);
ClientConferenceMessage deserializeClientConference(const std::vector<uint8_t>& data);
ClientVideoStreamMessage deserializeClientVideoStream(const std::vector<uint8_t>& data);

// Сериализация серверных сообщений
std::vector<uint8_t> serializeServerConferenceCreated(const ServerConferenceCreatedMessage& msg);
std::vector<uint8_t> serializeServerConferenceClosed(const ServerConferenceClosedMessage& msg);
std::vector<uint8_t> serializeServerConferenceJoined(const ServerConferenceJoinedMessage& msg);
std::vector<uint8_t> serializeServerParticipant(const ServerParticipantMessage& msg);
std::vector<uint8_t> serializeServerVideoStream(const ServerVideoStreamMessage& msg);

// Десериализация серверных сообщений
ServerConferenceCreatedMessage deserializeServerConferenceCreated(const std::vector<uint8_t>& data);
ServerConferenceClosedMessage deserializeServerConferenceClosed(const std::vector<uint8_t>& data);
ServerConferenceJoinedMessage deserializeServerConferenceJoined(const std::vector<uint8_t>& data);
ServerParticipantMessage deserializeServerParticipant(const std::vector<uint8_t>& data);
ServerVideoStreamMessage deserializeServerVideoStream(const std::vector<uint8_t>& data);

// Вспомогательные функции для работы со строками в бинарном формате
std::vector<uint8_t> stringToBytes(const std::string& str);
std::string bytesToString(const std::vector<uint8_t>& data, size_t& offset);
std::vector<uint8_t> stringsToBytes(const std::vector<std::string>& strings);
std::vector<std::string> bytesToStrings(const std::vector<uint8_t>& data, size_t& offset);

#endif
