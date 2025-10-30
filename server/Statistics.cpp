#include "Statistics.h"

std::atomic<long> Statistics::clientsConnected(0);
std::atomic<long> Statistics::clientsDisconnected(0);
std::atomic<long> Statistics::conferencesCreated(0);
std::atomic<long> Statistics::conferencesClosed(0);
std::atomic<long> Statistics::udpPacketsReceived(0);
std::atomic<long> Statistics::udpPacketsSent(0);
