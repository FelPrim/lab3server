#include "TcpConnection.h"
#include "Server.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <errno.h>

TcpConnection::TcpConnection(int socket, Server* server)
    : socket_(socket), server_(server), connected_(true) {
}

TcpConnection::~TcpConnection() {
    close();
}

void TcpConnection::start() {
    if (!connected_) return;
    
    // Начинаем асинхронное чтение
    doRead();
}

void TcpConnection::sendMessage(const std::vector<uint8_t>& message) {
    if (!connected_) return;
    
    // Добавляем длину сообщения в начало (4 байта)
    uint32_t messageLength = htonl(static_cast<uint32_t>(message.size()));
    std::vector<uint8_t> framedMessage;
    framedMessage.resize(sizeof(messageLength) + message.size());
    
    // Копируем длину сообщения
    memcpy(framedMessage.data(), &messageLength, sizeof(messageLength));
    // Копируем само сообщение
    if (!message.empty()) {
        memcpy(framedMessage.data() + sizeof(messageLength), message.data(), message.size());
    }
    
    // Отправляем сообщение
    ssize_t bytesSent = send(socket_, framedMessage.data(), framedMessage.size(), MSG_NOSIGNAL);
    if (bytesSent <= 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            close();
        }
    }
}

void TcpConnection::close() {
    if (!connected_) return;
    
    connected_ = false;
    if (socket_ != -1) {
        ::close(socket_);
        socket_ = -1;
    }
    
    if (disconnectHandler_) {
        disconnectHandler_();
    }
}

bool TcpConnection::isConnected() const {
    return connected_;
}

void TcpConnection::setMessageHandler(std::function<void(const std::vector<uint8_t>&)> handler) {
    messageHandler_ = handler;
}

void TcpConnection::setDisconnectHandler(std::function<void()> handler) {
    disconnectHandler_ = handler;
}

void TcpConnection::doRead() {
    if (!connected_) return;
    
    // Создаем буфер для чтения
    std::vector<uint8_t> buffer(BUFFER_SIZE);
    
    while (connected_) {
        ssize_t bytesRead = recv(socket_, buffer.data(), buffer.size(), 0);
        
        if (bytesRead > 0) {
            // Обрабатываем полученные данные
            handleReceivedData(buffer.data(), bytesRead);
        } else if (bytesRead == 0) {
            // Соединение закрыто клиентом
            std::cout << "Client disconnected gracefully" << std::endl;
            close();
            break;
        } else {
            // Ошибка чтения
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                std::cerr << "Error reading from socket: " << strerror(errno) << std::endl;
                close();
            }
            break;
        }
    }
}

void TcpConnection::handleReceivedData(const uint8_t* data, size_t length) {
    // Добавляем данные в буфер
    receiveBuffer_.insert(receiveBuffer_.end(), data, data + length);
    
    // Обрабатываем все полные сообщения в буфере
    while (connected_ && receiveBuffer_.size() >= sizeof(uint32_t)) {
        // Извлекаем длину сообщения
        uint32_t messageLength;
        memcpy(&messageLength, receiveBuffer_.data(), sizeof(messageLength));
        messageLength = ntohl(messageLength);
        
        // Проверяем, есть ли полное сообщение
        size_t totalLength = sizeof(messageLength) + messageLength;
        if (receiveBuffer_.size() < totalLength) {
            // Сообщение еще не полностью получено
            break;
        }
        
        // Извлекаем сообщение
        std::vector<uint8_t> message(
            receiveBuffer_.begin() + sizeof(messageLength),
            receiveBuffer_.begin() + totalLength
        );
        
        // Удаляем обработанное сообщение из буфера
        receiveBuffer_.erase(receiveBuffer_.begin(), receiveBuffer_.begin() + totalLength);
        
        // Обрабатываем сообщение
        handleMessage(message);
    }
}

void TcpConnection::handleMessage(const std::vector<uint8_t>& message) {
    if (messageHandler_ && !message.empty()) {
        messageHandler_(message);
    }
}

/*
 * Пример использования
 * Создание TCP соединения
auto connection = std::make_shared<TcpConnection>(clientSocket, server);

// Установка обработчиков
connection->setMessageHandler([this, connection](const std::vector<uint8_t>& message) {
    handleClientMessage(connection, message);
});

connection->setDisconnectHandler([this, connection]() {
    handleClientDisconnect(connection);
});

// Запуск обработки
connection->start();

// Отправка сообщения
std::vector<uint8_t> message = {1, 2, 3, 4};
connection->sendMessage(message);
 * */
