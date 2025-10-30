#ifndef TCPCONNECTION_H
#define TCPCONNECTION_H

#include <memory>
#include <functional>
#include <vector>
#include <cstdint>

class Server;

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(int socket, Server* server);
    ~TcpConnection();

    void start();
    void sendMessage(const std::vector<uint8_t>& message);
    void close();
    bool isConnected() const;

    // Callback для обработки входящих сообщений
    void setMessageHandler(std::function<void(const std::vector<uint8_t>&)> handler);
    void setDisconnectHandler(std::function<void()> handler);

    int getSocket() const { return socket_; }

private:
    void doRead();
    void handleReceivedData(const uint8_t* data, size_t length);
    void handleMessage(const std::vector<uint8_t>& message);

    int socket_;
    Server* server_;
    bool connected_;
    
    std::function<void(const std::vector<uint8_t>&)> messageHandler_;
    std::function<void()> disconnectHandler_;
    
    std::vector<uint8_t> receiveBuffer_;
    static const size_t BUFFER_SIZE = 4096;
};

#endif
