#ifndef CLIENTSESSION_H
#define CLIENTSESSION_H

#include <netinet/in.h>
#include <string>
#include <memory>
#include <vector>

class TcpConnection;

class ClientSession {
public:
    ClientSession(std::shared_ptr<TcpConnection> connection, const std::string& clientId);
    ~ClientSession();

    // Идентификация
    std::string getId() const;
    
    // UDP адрес
    void setUdpAddress(const sockaddr_in& address);
    sockaddr_in getUdpAddress() const;
    bool hasUdpAddress() const;
    
    // Управление конференциями
    void joinConference(const std::string& conferenceId);
    void leaveConference();
    std::string getCurrentConference() const;
    bool isInConference() const;
    bool isInConference(const std::string& conferenceId) const;
    
    // Управление видеостримами
    void addVideoStream(const std::string& streamId);
    void removeVideoStream(const std::string& streamId);
    std::vector<std::string> getVideoStreams() const;
    bool hasVideoStream(const std::string& streamId) const;
    size_t getVideoStreamCount() const;
    
    // Сетевые операции
    void sendMessage(const std::vector<uint8_t>& message);
    bool isConnected() const;
    void disconnect();
    
    // Управление соединением
    std::shared_ptr<TcpConnection> getConnection() const;
    void updateConnection(std::shared_ptr<TcpConnection> connection);
    
    // Операторы сравнения
    bool operator==(const ClientSession& other) const;
    bool operator!=(const ClientSession& other) const;

private:
    std::string clientId_;
    std::shared_ptr<TcpConnection> tcpConnection_;
    sockaddr_in udpAddress_;
    std::string currentConference_;
    std::vector<std::string> videoStreams_;
};

#endif
