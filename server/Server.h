#ifndef SERVER_H
#define SERVER_H

#include <memory>
#include <unordered_map>
#include <string>
#include <netinet/in.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

class TcpConnection;
class UdpConnection;
class Conference;
class ClientSession;

class Server {
public:
    Server();
    ~Server();

    // Основные методы
    bool start(int tcpPort, int udpPort);
    void stop();
    bool isRunning() const { return running_; }

    // Управление соединениями
    void handleNewTcpConnection();
    void handleTcpMessage(std::shared_ptr<TcpConnection> connection, const std::vector<uint8_t>& message);
    void handleTcpDisconnect(std::shared_ptr<TcpConnection> connection);
    void handleUdpPacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress);

    // Управление конференциями
    std::string createConference(std::shared_ptr<ClientSession> creator);
    bool joinConference(const std::string& conferenceId, std::shared_ptr<ClientSession> client);
    void leaveConference(const std::string& clientId);
    void closeConference(const std::string& conferenceId);

    // Управление клиентами
    void registerUdpClient(const std::string& clientId, const sockaddr_in& address);
    void unregisterUdpClient(const std::string& clientId);
    std::shared_ptr<ClientSession> getClient(const std::string& clientId) const;
    std::shared_ptr<Conference> getConference(const std::string& conferenceId) const;

    // Утилиты
    std::string generateClientId();
    std::string generateConferenceId();

private:
    // Инициализация сокетов
    bool setupTcpSocket(int port);
    bool setupUdpSocket(int port);
    
    // Основные циклы
    void runTcpAcceptLoop();
    void runUdpReceiveLoop();
    
    // Обработчики сообщений от клиентов
    void handleClientMessage(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& message);
    void processClientUdpAddress(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data);
    void processCreateConference(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data);
    void processJoinConference(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data);
    void processLeaveConference(std::shared_ptr<ClientSession> client);
    void processAddVideoStream(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data);
    void processRemoveVideoStream(std::shared_ptr<ClientSession> client, const std::vector<uint8_t>& data);
    
    // Отправка сообщений клиентам
    void sendConferenceCreated(std::shared_ptr<ClientSession> client, const std::string& conferenceId);
    void sendConferenceClosed(const std::string& conferenceId);
    void sendConferenceJoined(std::shared_ptr<ClientSession> client, const std::string& conferenceId);
    void sendNewParticipant(const std::string& conferenceId, const std::string& participantId);
    void sendParticipantLeft(const std::string& conferenceId, const std::string& participantId);
    void sendVideoStreamAdded(const std::string& conferenceId, const std::string& streamId, const std::string& participantId);
    void sendVideoStreamRemoved(const std::string& conferenceId, const std::string& streamId, const std::string& participantId);

    // Сокеты
    int tcpSocket_;
    int udpSocket_;
    int tcpPort_;
    int udpPort_;
    std::atomic<bool> running_;

    // Компоненты
    std::unique_ptr<UdpConnection> udpConnection_;
    
    // Данные
    std::unordered_map<std::string, std::shared_ptr<Conference>> conferences_;
    std::unordered_map<std::string, std::shared_ptr<ClientSession>> clients_;
    std::unordered_map<int, std::shared_ptr<TcpConnection>> tcpConnections_;
    std::unordered_map<std::string, std::shared_ptr<TcpConnection>> clientToTcpMap_; // clientId -> TcpConnection
    
    // Потоки
    std::thread tcpAcceptThread_;
    std::thread udpReceiveThread_;
    
    // Синхронизация
    mutable std::mutex clientsMutex_;
    mutable std::mutex conferencesMutex_;
    mutable std::mutex connectionsMutex_;
};

#endif
