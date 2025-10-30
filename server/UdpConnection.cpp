#include "UdpConnection.h"
#include "Server.h"
#include "ClientSession.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <errno.h>
#include <fcntl.h>
#include <stdexcept>

UdpConnection::UdpConnection(Server* server)
    : server_(server), socket_(-1) {
}

UdpConnection::~UdpConnection() {
    if (socket_ != -1) {
        close(socket_);
    }
}

bool UdpConnection::initialize(int port) {
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == -1) {
        std::cerr << "Failed to create UDP socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Устанавливаем опцию для повторного использования порта
    int reuse = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set UDP socket options: " << strerror(errno) << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }
    
    // Устанавливаем неблокирующий режим
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "Failed to get socket flags: " << strerror(errno) << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }
    
    if (fcntl(socket_, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set non-blocking mode: " << strerror(errno) << std::endl;
        // Не критичная ошибка, продолжаем
    }
    
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    
    if (::bind(socket_, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind UDP socket to port " << port << ": " << strerror(errno) << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }
    
    std::cout << "UDP connection initialized on port " << port << std::endl;
    return true;
}

void UdpConnection::handlePacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress) {
    if (data.empty()) {
        return;
    }
    
    try {
        routePacket(data, fromAddress);
    } catch (const std::exception& e) {
        std::cerr << "Error routing UDP packet: " << e.what() << std::endl;
    }
}

void UdpConnection::routePacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress) {
    // Парсим заголовок пакета для определения получателей
    auto recipients = parsePacketHeader(data);
    
    if (recipients.empty()) {
        std::cout << "No recipients found for UDP packet" << std::endl;
        return;
    }
    
    // Отправляем пакет всем получателям
    for (const auto& recipientId : recipients) {
        if (hasClient(recipientId)) {
            sendToClient(data, recipientId);
        } else {
            std::cout << "Recipient not found: " << recipientId << std::endl;
        }
    }
}

std::vector<std::string> UdpConnection::parsePacketHeader(const std::vector<uint8_t>& data) {
    std::vector<std::string> recipients;
    
    // Предполагаем, что первые 4 байта - это тип пакета
    if (data.size() < 4) {
        return recipients;
    }
    
    uint32_t packetType;
    memcpy(&packetType, data.data(), sizeof(packetType));
    
    // В зависимости от типа пакета, определяем логику маршрутизации
    switch (packetType) {
        case 1: { // Видео пакет - содержит ID конференции
            if (data.size() >= 36) { // 4 байта типа + 32 байта ID конференции
                std::string conferenceId(data.begin() + 4, data.begin() + 36);
                // Здесь должна быть логика получения всех участников конференции
                // Пока возвращаем пустой список - эту логику нужно интегрировать с Server
                break;
            }
            break;
        }
        case 2: { // Аудио пакет - содержит ID отправителя и получателя
            if (data.size() >= 68) { // 4 байта типа + 32 байта ID отправителя + 32 байта ID получателя
                std::string recipientId(data.begin() + 36, data.begin() + 68);
                recipients.push_back(recipientId);
                break;
            }
            break;
        }
        case 3: { // Широковещательный пакет для конференции
            if (data.size() >= 36) {
                std::string conferenceId(data.begin() + 4, data.begin() + 36);
                // Здесь должна быть логика получения всех участников конференции
                // Пока возвращаем пустой список
                break;
            }
            break;
        }
        default:
            std::cout << "Unknown packet type: " << packetType << std::endl;
            break;
    }
    
    return recipients;
}

void UdpConnection::sendToClient(const std::vector<uint8_t>& data, const std::string& clientId) {
    if (!hasClient(clientId)) {
        throw std::runtime_error("Client not registered: " + clientId);
    }
    
    const sockaddr_in& address = clientAddresses_[clientId];
    
    ssize_t bytesSent = sendto(
        socket_,
        data.data(),
        data.size(),
        0,
        (const sockaddr*)&address,
        sizeof(address)
    );
    
    if (bytesSent < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            throw std::runtime_error("Failed to send UDP packet: " + std::string(strerror(errno)));
        }
    } else if (bytesSent != static_cast<ssize_t>(data.size())) {
        std::cerr << "Partial UDP packet sent: " << bytesSent << " of " << data.size() << " bytes" << std::endl;
    }
}

void UdpConnection::registerClient(const std::string& clientId, const sockaddr_in& address) {
    clientAddresses_[clientId] = address;
    std::cout << "Registered UDP client: " << clientId << " at " 
              << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port) << std::endl;
}

void UdpConnection::unregisterClient(const std::string& clientId) {
    auto it = clientAddresses_.find(clientId);
    if (it != clientAddresses_.end()) {
        clientAddresses_.erase(it);
        std::cout << "Unregistered UDP client: " << clientId << std::endl;
    }
}

sockaddr_in UdpConnection::getClientAddress(const std::string& clientId) const {
    auto it = clientAddresses_.find(clientId);
    if (it != clientAddresses_.end()) {
        return it->second;
    }
    throw std::runtime_error("Client not found: " + clientId);
}

bool UdpConnection::hasClient(const std::string& clientId) const {
    return clientAddresses_.find(clientId) != clientAddresses_.end();
}

/*
 * Пример использования:
 * Инициализация UDP соединения
udpConnection_ = std::make_unique<UdpConnection>(this);
if (!udpConnection_->initialize(8081)) {
    std::cerr << "Failed to initialize UDP connection" << std::endl;
    return false;
}

// Регистрация клиента
sockaddr_in clientAddr;
clientAddr.sin_family = AF_INET;
clientAddr.sin_port = htons(12345);
inet_pton(AF_INET, "192.168.1.100", &clientAddr.sin_addr);
udpConnection_->registerClient("client123", clientAddr);

// Обработка входящего пакета (вызывается из UdpSocket)
void Server::handleUdpPacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress) {
    if (udpConnection_) {
        udpConnection_->handlePacket(data, fromAddress);
    }
}
 * */
