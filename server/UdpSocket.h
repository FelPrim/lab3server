#ifndef UDPSOCKET_H
#define UDPSOCKET_H

#include <memory>
#include <functional>
#include <vector>
#include <netinet/in.h>

class Server;

class UdpSocket {
public:
    UdpSocket(Server* server);
    ~UdpSocket();

    // Привязка сокета к порту
    bool bind(int port);
    
    // Начало приема пакетов (блокирующая операция)
    void startReceiving();
    
    // Отправка данных на указанный адрес
    void sendTo(const std::vector<uint8_t>& data, const sockaddr_in& address);
    
    // Закрытие сокета
    void close();
    
    // Получение файлового дескриптора сокета
    int getSocket() const { return socket_; }
    
    // Проверка, открыт ли сокет
    bool isBound() const { return socket_ != -1; }

private:
    int socket_;
    Server* server_;
    static const size_t MAX_UDP_PACKET_SIZE = 65507; // Максимальный размер UDP пакета
};

#endif
