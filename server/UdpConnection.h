#ifndef UDPCONNECTION_H
#define UDPCONNECTION_H

#include <vector>
#include <cstdint>
#include <netinet/in.h>
#include <unordered_map>
#include <memory>
#include <string>

class Server;
class ClientSession;

class UdpConnection {
public:
    UdpConnection(Server* server);
    ~UdpConnection();

    // Инициализация UDP сокета
    bool initialize(int port);
    
    // Обработка входящего пакета
    void handlePacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress);
    
    // Отправка пакета конкретному клиенту
    void sendToClient(const std::vector<uint8_t>& data, const std::string& clientId);
    
    // Регистрация клиента для маршрутизации
    void registerClient(const std::string& clientId, const sockaddr_in& address);
    
    // Удаление регистрации клиента
    void unregisterClient(const std::string& clientId);
    
    // Получение адреса клиента по ID
    sockaddr_in getClientAddress(const std::string& clientId) const;
    
    // Проверка существования клиента
    bool hasClient(const std::string& clientId) const;

private:
    // Парсинг заголовка пакета для определения получателей
    std::vector<std::string> parsePacketHeader(const std::vector<uint8_t>& data);
    
    // Маршрутизация пакета на основе его содержимого
    void routePacket(const std::vector<uint8_t>& data, const sockaddr_in& fromAddress);

    Server* server_;
    int socket_;
    std::unordered_map<std::string, sockaddr_in> clientAddresses_;
};

#endif
