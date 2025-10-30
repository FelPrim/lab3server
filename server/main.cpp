#include "Server.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <string>
#include <getopt.h>

std::unique_ptr<Server> g_server;

void signalHandler(int signal) {
    std::cout << std::endl << "Received signal " << signal << ", shutting down server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

void setupSignalHandlers() {
    std::signal(SIGINT, signalHandler);   // Ctrl+C
    std::signal(SIGTERM, signalHandler);  // kill command
    std::signal(SIGQUIT, signalHandler);  // Ctrl+backslash
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [OPTIONS]" << std::endl;
    std::cout << "Video Conference Server" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p, --tcp-port PORT    TCP port for control connections (default: 8080)" << std::endl;
    std::cout << "  -u, --udp-port PORT    UDP port for media streams (default: 8081)" << std::endl;
    std::cout << "  -h, --help             Show this help message" << std::endl;
    std::cout << "  -v, --verbose          Enable verbose logging" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << programName << " --tcp-port 9000 --udp-port 9001" << std::endl;
}

int main(int argc, char* argv[]) {
    // Параметры по умолчанию
    int tcpPort = 8080;
    int udpPort = 8081;
    bool verbose = false;

    // Парсинг аргументов командной строки
    static struct option longOptions[] = {
        {"tcp-port", required_argument, 0, 'p'},
        {"udp-port", required_argument, 0, 'u'},
        {"help", no_argument, 0, 'h'},
        {"verbose", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };

    int option;
    int optionIndex = 0;
    
    while ((option = getopt_long(argc, argv, "p:u:hv", longOptions, &optionIndex)) != -1) {
        switch (option) {
            case 'p':
                tcpPort = std::stoi(optarg);
                if (tcpPort < 1 || tcpPort > 65535) {
                    std::cerr << "Error: TCP port must be between 1 and 65535" << std::endl;
                    return 1;
                }
                break;
            case 'u':
                udpPort = std::stoi(optarg);
                if (udpPort < 1 || udpPort > 65535) {
                    std::cerr << "Error: UDP port must be between 1 and 65535" << std::endl;
                    return 1;
                }
                break;
            case 'v':
                verbose = true;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    // Проверка, чтобы порты не конфликтовали
    if (tcpPort == udpPort) {
        std::cerr << "Error: TCP and UDP ports cannot be the same" << std::endl;
        return 1;
    }

    // Настройка обработчиков сигналов
    setupSignalHandlers();

    std::cout << "=== Video Conference Server ===" << std::endl;
    std::cout << "TCP Control Port: " << tcpPort << std::endl;
    std::cout << "UDP Media Port: " << udpPort << std::endl;
    std::cout << "Verbose Mode: " << (verbose ? "enabled" : "disabled") << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    std::cout << "===============================" << std::endl;

    // Создание и запуск сервера
    g_server = std::make_unique<Server>();

    try {
        if (!g_server->start(tcpPort, udpPort)) {
            std::cerr << "Failed to start server" << std::endl;
            return 1;
        }

        std::cout << "Server started successfully" << std::endl;
        std::cout << "Waiting for connections..." << std::endl;

        // Основной цикл - просто ждем, пока сервер работает
        while (g_server->isRunning()) {
            // Можно добавить здесь дополнительную логику мониторинга
            // Например, вывод статистики каждые 30 секунд
            
            static time_t lastStatsTime = 0;
            time_t currentTime = time(nullptr);
            
            if (verbose && currentTime - lastStatsTime >= 30) {
                // Здесь можно выводить статистику сервера
                std::cout << "[STATS] Server is running..." << std::endl;
                lastStatsTime = currentTime;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::cout << "Server stopped gracefully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown server error occurred" << std::endl;
        return 1;
    }

    return 0;
}
