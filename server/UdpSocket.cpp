#include "UdpSocket.h"
#include "Server.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <errno.h>
#include <fcntl.h>

UdpSocket::UdpSocket(Server* server)
    : socket_(-1), server_(server) {
}

UdpSocket::~UdpSocket() {
    close();
}

bool UdpSocket::bind(int port) {
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == -1) {
        std::cerr << "Failed to create UDP socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Устанавливаем опцию для повторного использования порта
    int reuse = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set UDP socket options: " << strerror(errno) << std::endl;
        close();
        return false;
    }
    
    // Устанавливаем неблокирующий режим (опционально)
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "Failed to get socket flags: " << strerror(errno) << std::endl;
        close();
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
        close();
        return false;
    }
    
    std::cout << "UDP socket bound to port " << port << std::endl;
    return true;
}

void UdpSocket::startReceiving() {
    if (socket_ == -1) {
        std::cerr << "UDP socket is not bound" << std::endl;
        return;
    }
    
    std::cout << "Starting UDP packet reception..." << std::endl;
    
    std::vector<uint8_t> buffer(MAX_UDP_PACKET_SIZE);
    
    while (socket_ != -1) {
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        
        ssize_t bytesReceived = recvfrom(
            socket_, 
            buffer.data(), 
            buffer.size(), 
            0,
            (sockaddr*)&clientAddr, 
            &clientAddrLen
        );
        
        if (bytesReceived > 0) {
            // Копируем данные и адрес отправителя
            std::vector<uint8_t> data(buffer.begin(), buffer.begin() + bytesReceived);
            
            // Передаем пакет на обработку серверу
            if (server_) {
                server_->handleUdpPacket(data, clientAddr);
            }
        } else if (bytesReceived < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                // Нет данных для чтения - продолжаем цикл
                continue;
            } else {
                std::cerr << "Error receiving UDP packet: " << strerror(errno) << std::endl;
                break;
            }
        }
        // bytesReceived == 0 - UDP обычно не возвращает 0, но если вернет, игнорируем
    }
    
    std::cout << "UDP packet reception stopped" << std::endl;
}

void UdpSocket::sendTo(const std::vector<uint8_t>& data, const sockaddr_in& address) {
    if (socket_ == -1) {
        std::cerr << "Cannot send - UDP socket is not bound" << std::endl;
        return;
    }
    
    if (data.empty()) {
        return; // Не отправляем пустые пакеты
    }
    
    if (data.size() > MAX_UDP_PACKET_SIZE) {
        std::cerr << "UDP packet too large: " << data.size() << " bytes (max: " << MAX_UDP_PACKET_SIZE << ")" << std::endl;
        return;
    }
    
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
            std::cerr << "Error sending UDP packet: " << strerror(errno) << std::endl;
        }
        // В неблокирующем режиме EWOULDBLOCK/EAGAIN - это нормально
    } else if (bytesSent != static_cast<ssize_t>(data.size())) {
        std::cerr << "Partial UDP packet sent: " << bytesSent << " of " << data.size() << " bytes" << std::endl;
    }
}

void UdpSocket::close() {
    if (socket_ != -1) {
        ::close(socket_);
        socket_ = -1;
        std::cout << "UDP socket closed" << std::endl;
    }
}

/*
 * Пример использования:
 * В классе Server
void Server::handleUdpPacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress) {
    if (data.size() < sizeof(uint8_t)) {
        return; // Слишком короткий пакет
    }
    
    // Первый байт - тип пакета или идентификатор клиента
    uint8_t packetType = data[0];
    
    // Здесь логика маршрутизации UDP пакетов
    // Например, поиск клиента по адресу и пересылка пакета другим участникам конференции
    
    // Пример: пересылка видео пакета всем участникам конференции отправителя
    auto client = findClientByUdpAddress(fromAddress);
    if (client && client->isInConference()) {
        auto conference = conferences_[client->getCurrentConference()];
        auto participants = conference->getAllParticipants();
        
        for (auto& participant : participants) {
            if (participant->getId() != client->getId() && participant->hasUdpAddress()) {
                udpSocket_->sendTo(data, participant->getUdpAddress());
            }
        }
    }
}
 * */
