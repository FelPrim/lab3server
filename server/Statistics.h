#ifndef STATISTICS_H
#define STATISTICS_H

#include <atomic>
#include <string>

class Statistics {
public:
    static void clientConnected() { ++clientsConnected; }
    static void clientDisconnected() { ++clientsDisconnected; }
    static void conferenceCreated() { ++conferencesCreated; }
    static void conferenceClosed() { ++conferencesClosed; }
    static void udpPacketReceived() { ++udpPacketsReceived; }
    static void udpPacketSent() { ++udpPacketsSent; }

    static void printStats() {
        std::cout << "=== Server Statistics ===" << std::endl;
        std::cout << "Clients connected: " << clientsConnected << std::endl;
        std::cout << "Clients disconnected: " << clientsDisconnected << std::endl;
        std::cout << "Active clients: " << (clientsConnected - clientsDisconnected) << std::endl;
        std::cout << "Conferences created: " << conferencesCreated << std::endl;
        std::cout << "Conferences closed: " << conferencesClosed << std::endl;
        std::cout << "UDP packets received: " << udpPacketsReceived << std::endl;
        std::cout << "UDP packets sent: " << udpPacketsSent << std::endl;
        std::cout << "=========================" << std::endl;
    }

private:
    static std::atomic<long> clientsConnected;
    static std::atomic<long> clientsDisconnected;
    static std::atomic<long> conferencesCreated;
    static std::atomic<long> conferencesClosed;
    static std::atomic<long> udpPacketsReceived;
    static std::atomic<long> udpPacketsSent;
};

#endif
