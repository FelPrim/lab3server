#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

// Объявления тестовых функций
void run_all_buffer_tests();
void run_all_network_tests();
void run_all_connection_tests();
void run_all_stream_tests();

void handle_signal(int sig) {
    printf("\nReceived signal %d, stopping tests...\n", sig);
    exit(1);
}

int main() {
    printf("========================================\n");
    printf("Starting Comprehensive Network Tests\n");
    printf("========================================\n\n");
    
    // Устанавливаем обработчик сигналов для корректного завершения
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Запускаем все тесты
    run_all_buffer_tests();
    run_all_network_tests();
    run_all_connection_tests();
    run_all_stream_tests();
    
    printf("========================================\n");
    printf("All Tests Completed Successfully! ✓\n");
    printf("========================================\n");
    
    return 0;
}