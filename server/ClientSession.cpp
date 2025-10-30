#include "ClientSession.h"
#include "TcpConnection.h"
#include <stdexcept>
#include <cstring>

ClientSession::ClientSession(std::shared_ptr<TcpConnection> connection, const std::string& clientId)
    : clientId_(clientId), tcpConnection_(connection) {
    memset(&udpAddress_, 0, sizeof(udpAddress_));
    udpAddress_.sin_family = AF_INET;
}

ClientSession::~ClientSession() {
    leaveConference();
}

std::string ClientSession::getId() const {
    return clientId_;
}

void ClientSession::setUdpAddress(const sockaddr_in& address) {
    if (address.sin_family != AF_INET) {
        throw std::runtime_error("Invalid address family");
    }
    udpAddress_ = address;
}

sockaddr_in ClientSession::getUdpAddress() const {
    return udpAddress_;
}

bool ClientSession::hasUdpAddress() const {
    return udpAddress_.sin_port != 0 && udpAddress_.sin_addr.s_addr != 0;
}

void ClientSession::joinConference(const std::string& conferenceId) {
    currentConference_ = conferenceId;
    // Очищаем стримы при присоединении к новой конференции
    videoStreams_.clear();
}

void ClientSession::leaveConference() {
    currentConference_.clear();
    videoStreams_.clear();
}

std::string ClientSession::getCurrentConference() const {
    return currentConference_;
}

bool ClientSession::isInConference() const {
    return !currentConference_.empty();
}

bool ClientSession::isInConference(const std::string& conferenceId) const {
    return currentConference_ == conferenceId;
}

void ClientSession::addVideoStream(const std::string& streamId) {
    // Проверяем, нет ли уже такого стрима
    for (const auto& stream : videoStreams_) {
        if (stream == streamId) {
            return; // Стрим уже существует
        }
    }
    videoStreams_.push_back(streamId);
}

void ClientSession::removeVideoStream(const std::string& streamId) {
    videoStreams_.erase(
        std::remove(videoStreams_.begin(), videoStreams_.end(), streamId),
        videoStreams_.end()
    );
}

std::vector<std::string> ClientSession::getVideoStreams() const {
    return videoStreams_;
}

bool ClientSession::hasVideoStream(const std::string& streamId) const {
    for (const auto& stream : videoStreams_) {
        if (stream == streamId) {
            return true;
        }
    }
    return false;
}

size_t ClientSession::getVideoStreamCount() const {
    return videoStreams_.size();
}

void ClientSession::sendMessage(const std::vector<uint8_t>& message) {
    if (tcpConnection_) {
        tcpConnection_->sendMessage(message);
    }
}

bool ClientSession::isConnected() const {
    return tcpConnection_ != nullptr;
}

void ClientSession::disconnect() {
    if (tcpConnection_) {
        tcpConnection_->close();
        tcpConnection_.reset();
    }
    leaveConference();
}

std::shared_ptr<TcpConnection> ClientSession::getConnection() const {
    return tcpConnection_;
}

void ClientSession::updateConnection(std::shared_ptr<TcpConnection> connection) {
    tcpConnection_ = connection;
}

bool ClientSession::operator==(const ClientSession& other) const {
    return clientId_ == other.clientId_;
}

bool ClientSession::operator!=(const ClientSession& other) const {
    return !(*this == other);
}

/*
 * Пример использования:
 *
 * Создание клиентской сессии
auto connection = std::make_shared<TcpConnection>(socket, server);
auto client = std::make_shared<ClientSession>(connection, "client123");

// Установка UDP адреса
sockaddr_in udpAddr;
udpAddr.sin_family = AF_INET;
udpAddr.sin_port = htons(8081);
inet_pton(AF_INET, "192.168.1.100", &udpAddr.sin_addr);
client->setUdpAddress(udpAddr);

// Присоединение к конференции
client->joinConference("conf123");

// Добавление видеострима
client->addVideoStream("stream1");

// Отправка сообщения
std::vector<uint8_t> message = {1, 2, 3, 4};
client->sendMessage(message);

// Проверка состояния
if (client->isInConference() && client->hasUdpAddress()) {
    // Клиент готов к видеосвязи
}
 * */
