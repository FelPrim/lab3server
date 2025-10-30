#include "Server.h"
#include "TcpConnection.h"
#include "UdpConnection.h"
#include "ClientSession.h"
#include "Conference.h"
#include "Message.h"
#include "IdGenerator.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <algorithm>

Server::Server() 
    : tcpSocket_(-1), udpSocket_(-1), tcpPort_(0), udpPort_(0), running_(false) {
}

Server::~Server() {
    stop();
}

bool Server::start(int tcpPort, int udpPort) {
    if (running_) {
        return true;
    }

    tcpPort_ = tcpPort;
    udpPort_ = udpPort;

    // Инициализация TCP сокета
    if (!setupTcpSocket(tcpPort)) {
        std::cerr << "Failed to setup TCP socket" << std::endl;
        return false;
    }

    // Инициализация UDP соединения
    udpConnection_ = std::make_unique<UdpConnection>(this);
    if (!udpConnection_->initialize(udpPort)) {
        std::cerr << "Failed to initialize UDP connection" << std::endl;
        return false;
    }

    running_ = true;

    // Запуск потоков
    tcpAcceptThread_ = std::thread(&Server::runTcpAcceptLoop, this);
    udpReceiveThread_ = std::thread(&Server::runUdpReceiveLoop, this);

    std::cout << "Server started on TCP port " << tcpPort << " and UDP port " << udpPort << std::endl;
    return true;
}

void Server::stop() {
    if (!running_) return;

    running_ = false;

    // Закрываем TCP сокет для прерывания accept
    if (tcpSocket_ != -1) {
        close(tcpSocket_);
        tcpSocket_ = -1;
    }

    // Ожидаем завершения потоков
    if (tcpAcceptThread_.joinable()) {
        tcpAcceptThread_.join();
    }
    if (udpReceiveThread_.joinable()) {
        udpReceiveThread_.join();
    }

    // Закрываем все соединения
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        tcpConnections_.clear();
        clientToTcpMap_.clear();
    }

    // Очищаем клиентов и конференции
    {
        std::lock_guard<std::mutex> lock1(clientsMutex_);
        std::lock_guard<std::mutex> lock2(conferencesMutex_);
        clients_.clear();
        conferences_.clear();
    }

    std::cout << "Server stopped" << std::endl;
}

bool Server::setupTcpSocket(int port) {
    tcpSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpSocket_ == -1) {
        std::cerr << "Failed to create TCP socket: " << strerror(errno) << std::endl;
        return false;
    }

    // Устанавливаем опцию повторного использования адреса
    int reuse = 1;
    if (setsockopt(tcpSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set TCP socket options: " << strerror(errno) << std::endl;
        close(tcpSocket_);
        return false;
    }

    // Устанавливаем неблокирующий режим
    int flags = fcntl(tcpSocket_, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "Failed to get TCP socket flags: " << strerror(errno) << std::endl;
        close(tcpSocket_);
        return false;
    }
    if (fcntl(tcpSocket_, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set TCP non-blocking mode: " << strerror(errno) << std::endl;
        close(tcpSocket_);
        return false;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(tcpSocket_, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Failed to bind TCP socket: " << strerror(errno) << std::endl;
        close(tcpSocket_);
        return false;
    }

    if (listen(tcpSocket_, SOMAXCONN) < 0) {
        std::cerr << "Failed to listen on TCP socket: " << strerror(errno) << std::endl;
        close(tcpSocket_);
        return false;
    }

    return true;
}

void Server::runTcpAcceptLoop() {
    while (running_) {
        handleNewTcpConnection();
        // Небольшая пауза чтобы не грузить CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Server::runUdpReceiveLoop() {
    // UDP прием обрабатывается в UdpConnection
    // Здесь можно добавить дополнительную логику если нужно
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Server::handleNewTcpConnection() {
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    
    int clientSocket = accept(tcpSocket_, (sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSocket < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            std::cerr << "Error accepting connection: " << strerror(errno) << std::endl;
        }
        return;
    }

    // Устанавливаем неблокирующий режим для клиентского сокета
    int flags = fcntl(clientSocket, F_GETFL, 0);
    if (flags != -1) {
        fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
    }

    std::cout << "New TCP connection from " 
              << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << std::endl;

    // Создаем TCP соединение
    auto connection = std::make_shared<TcpConnection>(clientSocket, this);
    
    // Создаем клиентскую сессию
    std::string clientId = generateClientId();
    auto clientSession = std::make_shared<ClientSession>(connection, clientId);

    // Регистрируем соединения
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        tcpConnections_[clientSocket] = connection;
        clientToTcpMap_[clientId] = connection;
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_[clientId] = clientSession;
    }

    // Настраиваем обработчики
    connection->setMessageHandler([this, connection](const std::vector<uint8_t>& message) {
        handleTcpMessage(connection, message);
    });

    connection->setDisconnectHandler([this, connection]() {
        handleTcpDisconnect(connection);
    });

    // Запускаем соединение
    connection->start();

    std::cout << "Client session created: " << clientId << std::endl;
}

void Server::handleTcpMessage(std::shared_ptr<TcpConnection> connection, const std::vector<uint8_t>& message) {
    if (message.empty()) return;

    // Находим клиента по соединению
    std::string clientId;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        for (const auto& [id, conn] : clientToTcpMap_) {
            if (conn == connection) {
                clientId = id;
                break;
            }
        }
    }

    if (clientId.empty()) {
        std::cerr << "Received message from unknown connection" << std::endl;
        return;
    }

    std::shared_ptr<ClientSession> client;
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto it = clients_.find(clientId);
        if (it == clients_.end()) {
            std::cerr << "Client not found: " << clientId << std::endl;
            return;
        }
        client = it->second;
    }

    handleClientMessage(client, message);
}

void Server::handleTcpDisconnect(std::shared_ptr<TcpConnection> connection) {
    std::string clientId;
    
    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        for (auto it = clientToTcpMap_.begin(); it != clientToTcpMap_.end(); ) {
            if (it->second == connection) {
                clientId = it->first;
                it = clientToTcpMap_.erase(it);
                break;
            } else {
                ++it;
            }
        }

        for (auto it = tcpConnections_.begin(); it != tcpConnections_.end(); ) {
            if (it->second == connection) {
                it = tcpConnections_.erase(it);
                break;
            } else {
                ++it;
            }
        }
    }

    if (!clientId.empty()) {
        // Удаляем клиента и выходим из конференции
        leaveConference(clientId);
        
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients_.erase(clientId);
        }

        if (udpConnection_) {
            udpConnection_->unregisterClient(clientId);
        }

        std::cout << "Client disconnected: " << clientId << std::endl;
    }
}

void Server::handleUdpPacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress) {
    if (udpConnection_) {
        udpConnection_->handlePacket(data, fromAddress);
    }
}

void Server::handleClientMessage(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& message) {
    if (message.empty()) return;

    Message parsed = parseMessage(message);
    ClientMessageType type = static_cast<ClientMessageType>(parsed.type);

    try {
        switch (type) {
            case ClientMessageType::UDP_ADDRESS:
                processClientUdpAddress(client, parsed.data);
                break;
            case ClientMessageType::CREATE_CONFERENCE:
                processCreateConference(client, parsed.data);
                break;
            case ClientMessageType::JOIN_CONFERENCE:
                processJoinConference(client, parsed.data);
                break;
            case ClientMessageType::LEAVE_CONFERENCE:
                processLeaveConference(client);
                break;
            case ClientMessageType::ADD_VIDEO_STREAM:
                processAddVideoStream(client, parsed.data);
                break;
            case ClientMessageType::REMOVE_VIDEO_STREAM:
                processRemoveVideoStream(client, parsed.data);
                break;
            case ClientMessageType::END_CONFERENCE:
                // Обработка завершения конференции
                if (client->isInConference()) {
                    closeConference(client->getCurrentConference());
                }
                break;
            case ClientMessageType::DISCONNECT:
                // Клиент запросил отключение
                client->disconnect();
                break;
            default:
                std::cout << "Unknown message type from client: " << static_cast<int>(type) << std::endl;
                break;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing client message: " << e.what() << std::endl;
    }
}

void Server::processClientUdpAddress(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data) {
    try {
        ClientUdpAddressMessage udpMsg = deserializeClientUdpAddress(data);
        sockaddr_in address = udpMsg.address.toSockaddr();
        
        client->setUdpAddress(address);
        
        if (udpConnection_) {
            udpConnection_->registerClient(client->getId(), address);
        }
        
        std::cout << "Registered UDP address for client " << client->getId() 
                  << ": " << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port) << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error processing UDP address: " << e.what() << std::endl;
    }
}

void Server::processCreateConference(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data) {
    std::string conferenceId = generateConferenceId();
    
    {
        std::lock_guard<std::mutex> lock(conferencesMutex_);
        auto conference = std::make_shared<Conference>(conferenceId, client->getId());
        conferences_[conferenceId] = conference;
        
        // Добавляем создателя в конференцию
        conference->addParticipant(client);
    }
    
    sendConferenceCreated(client, conferenceId);
    std::cout << "Conference created: " << conferenceId << " by client " << client->getId() << std::endl;
}

void Server::processJoinConference(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data) {
    try {
        ClientConferenceMessage joinMsg = deserializeClientConference(data);
        std::string conferenceId = joinMsg.conferenceId;
        
        if (joinConference(conferenceId, client)) {
            sendConferenceJoined(client, conferenceId);
            sendNewParticipant(conferenceId, client->getId());
            std::cout << "Client " << client->getId() << " joined conference " << conferenceId << std::endl;
        } else {
            std::cout << "Client " << client->getId() << " failed to join conference " << conferenceId << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing join conference: " << e.what() << std::endl;
    }
}

void Server::processLeaveConference(std::shared_ptr<ClientSession> client) {
    leaveConference(client->getId());
    std::cout << "Client " << client->getId() << " left conference" << std::endl;
}

void Server::processAddVideoStream(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data) {
    try {
        ClientVideoStreamMessage streamMsg = deserializeClientVideoStream(data);
        
        std::lock_guard<std::mutex> lock(conferencesMutex_);
        auto it = conferences_.find(streamMsg.conferenceId);
        if (it != conferences_.end() && it->second->hasParticipant(client->getId())) {
            it->second->addVideoStream(client->getId(), streamMsg.streamId);
            sendVideoStreamAdded(streamMsg.conferenceId, streamMsg.streamId, client->getId());
            std::cout << "Video stream added: " << streamMsg.streamId << " by client " << client->getId() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error adding video stream: " << e.what() << std::endl;
    }
}

void Server::processRemoveVideoStream(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data) {
    try {
        ClientVideoStreamMessage streamMsg = deserializeClientVideoStream(data);
        
        std::lock_guard<std::mutex> lock(conferencesMutex_);
        auto it = conferences_.find(streamMsg.conferenceId);
        if (it != conferences_.end() && it->second->hasParticipant(client->getId())) {
            it->second->removeVideoStream(client->getId(), streamMsg.streamId);
            sendVideoStreamRemoved(streamMsg.conferenceId, streamMsg.streamId, client->getId());
            std::cout << "Video stream removed: " << streamMsg.streamId << " by client " << client->getId() << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error removing video stream: " << e.what() << std::endl;
    }
}

// Реализация методов управления конференциями
std::string Server::createConference(std::shared_ptr<ClientSession> creator) {
    std::string conferenceId = generateConferenceId();
    
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    auto conference = std::make_shared<Conference>(conferenceId, creator->getId());
    conferences_[conferenceId] = conference;
    
    return conferenceId;
}

bool Server::joinConference(const std::string& conferenceId, std::shared_ptr<ClientSession> client) {
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    
    auto it = conferences_.find(conferenceId);
    if (it == conferences_.end() || !it->second->isActive()) {
        return false;
    }
    
    return it->second->addParticipant(client);
}

void Server::leaveConference(const std::string& clientId) {
    std::string conferenceId;
    
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        auto clientIt = clients_.find(clientId);
        if (clientIt != clients_.end()) {
            conferenceId = clientIt->second->getCurrentConference();
            clientIt->second->leaveConference();
        }
    }
    
    if (!conferenceId.empty()) {
        std::lock_guard<std::mutex> lock(conferencesMutex_);
        auto confIt = conferences_.find(conferenceId);
        if (confIt != conferences_.end()) {
            confIt->second->removeParticipant(clientId);
            sendParticipantLeft(conferenceId, clientId);
        }
    }
}

void Server::closeConference(const std::string& conferenceId) {
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    
    auto it = conferences_.find(conferenceId);
    if (it != conferences_.end()) {
        sendConferenceClosed(conferenceId);
        it->second->close();
        conferences_.erase(it);
        std::cout << "Conference closed: " << conferenceId << std::endl;
    }
}

// Реализация методов отправки сообщений
void Server::sendConferenceCreated(std::shared_ptr<ClientSession> client, const std::string& conferenceId) {
    ServerConferenceCreatedMessage msg;
    msg.conferenceId = conferenceId;
    
    auto data = serializeServerConferenceCreated(msg);
    auto message = serializeMessage(static_cast<uint8_t>(ServerMessageType::CONFERENCE_CREATED), data);
    
    client->sendMessage(message);
}

void Server::sendConferenceClosed(const std::string& conferenceId) {
    ServerConferenceClosedMessage msg;
    msg.conferenceId = conferenceId;
    
    auto data = serializeServerConferenceClosed(msg);
    auto message = serializeMessage(static_cast<uint8_t>(ServerMessageType::CONFERENCE_CLOSED), data);
    
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    auto it = conferences_.find(conferenceId);
    if (it != conferences_.end()) {
        auto participants = it->second->getAllParticipants();
        for (auto& participant : participants) {
            participant->sendMessage(message);
        }
    }
}

void Server::sendConferenceJoined(std::shared_ptr<ClientSession> client, const std::string& conferenceId) {
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    auto it = conferences_.find(conferenceId);
    if (it == conferences_.end()) return;
    
    ServerConferenceJoinedMessage msg;
    msg.conferenceId = conferenceId;
    msg.participants = it->second->getParticipants();
    msg.videoStreams = it->second->getVideoStreams();
    
    auto data = serializeServerConferenceJoined(msg);
    auto message = serializeMessage(static_cast<uint8_t>(ServerMessageType::CONFERENCE_JOINED), data);
    
    client->sendMessage(message);
}

void Server::sendNewParticipant(const std::string& conferenceId, const std::string& participantId) {
    ServerParticipantMessage msg;
    msg.conferenceId = conferenceId;
    msg.participantId = participantId;
    
    auto data = serializeServerParticipant(msg);
    auto message = serializeMessage(static_cast<uint8_t>(ServerMessageType::NEW_PARTICIPANT), data);
    
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    auto it = conferences_.find(conferenceId);
    if (it != conferences_.end()) {
        auto participants = it->second->getAllParticipants();
        for (auto& participant : participants) {
            if (participant->getId() != participantId) {
                participant->sendMessage(message);
            }
        }
    }
}

void Server::sendParticipantLeft(const std::string& conferenceId, const std::string& participantId) {
    ServerParticipantMessage msg;
    msg.conferenceId = conferenceId;
    msg.participantId = participantId;
    
    auto data = serializeServerParticipant(msg);
    auto message = serializeMessage(static_cast<uint8_t>(ServerMessageType::PARTICIPANT_LEFT), data);
    
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    auto it = conferences_.find(conferenceId);
    if (it != conferences_.end()) {
        auto participants = it->second->getAllParticipants();
        for (auto& participant : participants) {
            participant->sendMessage(message);
        }
    }
}

void Server::sendVideoStreamAdded(const std::string& conferenceId, const std::string& streamId, const std::string& participantId) {
    ServerVideoStreamMessage msg;
    msg.conferenceId = conferenceId;
    msg.streamId = streamId;
    msg.participantId = participantId;
    
    auto data = serializeServerVideoStream(msg);
    auto message = serializeMessage(static_cast<uint8_t>(ServerMessageType::VIDEO_STREAM_ADDED), data);
    
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    auto it = conferences_.find(conferenceId);
    if (it != conferences_.end()) {
        auto participants = it->second->getAllParticipants();
        for (auto& participant : participants) {
            if (participant->getId() != participantId) {
                participant->sendMessage(message);
            }
        }
    }
}

void Server::sendVideoStreamRemoved(const std::string& conferenceId, const std::string& streamId, const std::string& participantId) {
    ServerVideoStreamMessage msg;
    msg.conferenceId = conferenceId;
    msg.streamId = streamId;
    msg.participantId = participantId;
    
    auto data = serializeServerVideoStream(msg);
    auto message = serializeMessage(static_cast<uint8_t>(ServerMessageType::VIDEO_STREAM_REMOVED), data);
    
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    auto it = conferences_.find(conferenceId);
    if (it != conferences_.end()) {
        auto participants = it->second->getAllParticipants();
        for (auto& participant : participants) {
            if (participant->getId() != participantId) {
                participant->sendMessage(message);
            }
        }
    }
}

// Утилиты
void Server::registerUdpClient(const std::string& clientId, const sockaddr_in& address) {
    if (udpConnection_) {
        udpConnection_->registerClient(clientId, address);
    }
}

void Server::unregisterUdpClient(const std::string& clientId) {
    if (udpConnection_) {
        udpConnection_->unregisterClient(clientId);
    }
}

std::shared_ptr<ClientSession> Server::getClient(const std::string& clientId) const {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(clientId);
    return it != clients_.end() ? it->second : nullptr;
}

std::shared_ptr<Conference> Server::getConference(const std::string& conferenceId) const {
    std::lock_guard<std::mutex> lock(conferencesMutex_);
    auto it = conferences_.find(conferenceId);
    return it != conferences_.end() ? it->second : nullptr;
}

std::string Server::generateClientId() {
    return IdGenerator::generateClientId();
}

std::string Server::generateConferenceId() {
    return IdGenerator::generateConferenceId();
}


/*
 * Пример использования:
 *int main() {
    Server server;
    
    if (server.start(8080, 8081)) {
        std::cout << "Server started successfully. Press Enter to stop..." << std::endl;
        std::cin.get();
        server.stop();
    } else {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    return 0;
}
 * */
