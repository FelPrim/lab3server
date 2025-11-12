# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -std=c23 -g -O0 -I.
LDFLAGS = -lpthread

# Папки
SRCDIR = .
OBJDIR = obj
BINDIR = bin
TESTDIR = test

# Исходные файлы основного проекта
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Тестовые файлы
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TESTDIR)/%.c=$(OBJDIR)/test_%.o)

# Имена исполняемых файлов
TARGET = $(BINDIR)/server
TEST_RUNNER = $(BINDIR)/test_runner

# Основная цель
all: $(TARGET)

# Сборка основного сервера
$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Сборка тестового раннера
test: $(TEST_RUNNER)

$(TEST_RUNNER): $(TEST_OBJECTS) $(filter-out $(OBJDIR)/main.o, $(OBJECTS)) | $(BINDIR)
	$(CC) $(TEST_OBJECTS) $(filter-out $(OBJDIR)/main.o, $(OBJECTS)) -o $@ $(LDFLAGS)

# Компиляция объектных файлов основного проекта
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Компиляция объектных файлов тестов (с указанием пути к заголовкам)
$(OBJDIR)/test_%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Создание папок
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

# Очистка
clean:
	rm -rf $(OBJDIR) $(BINDIR)

# Запуск всех тестов
run-test: test
	./$(TEST_RUNNER)

# Запуск сервера
run: $(TARGET)
	./$(TARGET)

# Отладочная информация
debug:
	@echo "Main Sources: $(SOURCES)"
	@echo "Main Objects: $(OBJECTS)"
	@echo "Test Sources: $(TEST_SOURCES)"
	@echo "Test Objects: $(TEST_OBJECTS)"

# Зависимости основного проекта
$(OBJDIR)/main.o: $(SRCDIR)/main.c $(SRCDIR)/network.h $(SRCDIR)/connection.h $(SRCDIR)/handlers.h $(SRCDIR)/protocol.h
$(OBJDIR)/protocol.o: $(SRCDIR)/protocol.c $(SRCDIR)/protocol.h $(SRCDIR)/handlers.h $(SRCDIR)/connection.h $(SRCDIR)/stream.h $(SRCDIR)/network.h
$(OBJDIR)/buffer.o: $(SRCDIR)/buffer.c $(SRCDIR)/buffer.h $(SRCDIR)/protocol.h
$(OBJDIR)/network.o: $(SRCDIR)/network.c $(SRCDIR)/network.h
$(OBJDIR)/stream.o: $(SRCDIR)/stream.c $(SRCDIR)/stream.h $(SRCDIR)/connection.h $(SRCDIR)/protocol.h $(SRCDIR)/handlers.h
$(OBJDIR)/connection.o: $(SRCDIR)/connection.c $(SRCDIR)/connection.h $(SRCDIR)/buffer.h $(SRCDIR)/stream.h $(SRCDIR)/network.h $(SRCDIR)/protocol.h $(SRCDIR)/handlers.h
$(OBJDIR)/handlers.o: $(SRCDIR)/handlers.c $(SRCDIR)/handlers.h $(SRCDIR)/protocol.h $(SRCDIR)/connection.h $(SRCDIR)/stream.h

# Зависимости тестов
$(OBJDIR)/test_buffer.o: $(TESTDIR)/test_buffer.c $(SRCDIR)/buffer.h
$(OBJDIR)/test_network.o: $(TESTDIR)/test_network.c $(SRCDIR)/network.h
$(OBJDIR)/test_main.o: $(TESTDIR)/test_main.c
$(OBJDIR)/test_connection.o: $(TESTDIR)/test_connection.c $(SRCDIR)/connection.h $(SRCDIR)/buffer.h $(SRCDIR)/stream.h
$(OBJDIR)/test_stream.o: $(TESTDIR)/test_stream.c $(SRCDIR)/stream.h $(SRCDIR)/connection.h

# Установка зависимостей (для Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install libssl-dev

# Специальные цели для отдельных тестов
test-buffer: $(OBJDIR)/test_buffer.o $(OBJDIR)/buffer.o | $(BINDIR)
	$(CC) $(OBJDIR)/test_buffer.o $(OBJDIR)/buffer.o -o $(BINDIR)/test_buffer $(LDFLAGS)

test-network: $(OBJDIR)/test_network.o $(OBJDIR)/network.o | $(BINDIR)
	$(CC) $(OBJDIR)/test_network.o $(OBJDIR)/network.o -o $(BINDIR)/test_network $(LDFLAGS)

run-test-buffer: test-buffer
	./$(BINDIR)/test_buffer

run-test-network: test-network
	./$(BINDIR)/test_network

# Тесты интеграции (требуют больше модулей)
test-integration: $(OBJDIR)/test_main.o $(OBJDIR)/buffer.o $(OBJDIR)/network.o | $(BINDIR)
	$(CC) $(OBJDIR)/test_main.o $(OBJDIR)/buffer.o $(OBJDIR)/network.o -o $(BINDIR)/test_integration $(LDFLAGS)

run-test-integration: test-integration
	./$(BINDIR)/test_integration

test-connection: $(OBJDIR)/test_connection.o $(OBJDIR)/connection.o $(OBJDIR)/buffer.o $(OBJDIR)/stream.o | $(BINDIR)
	$(CC) $(OBJDIR)/test_connection.o $(OBJDIR)/connection.o $(OBJDIR)/buffer.o $(OBJDIR)/stream.o -o $(BINDIR)/test_connection $(LDFLAGS)

test-stream: $(OBJDIR)/test_stream.o $(OBJDIR)/stream.o $(OBJDIR)/connection.o $(OBJDIR)/buffer.o | $(BINDIR)
	$(CC) $(OBJDIR)/test_stream.o $(OBJDIR)/stream.o $(OBJDIR)/connection.o $(OBJDIR)/buffer.o -o $(BINDIR)/test_stream $(LDFLAGS)

run-test-connection: test-connection
	./$(BINDIR)/test_connection

run-test-stream: test-stream
	./$(BINDIR)/test_stream

# Помощь
help:
	@echo "Доступные цели:"
	@echo "  all                - собрать основной сервер"
	@echo "  test               - собрать все тесты"
	@echo "  run-test           - собрать и запустить все тесты"
	@echo "  run                - собрать и запустить сервер"
	@echo "  clean              - очистить сборочные артефакты"
	@echo "  test-buffer        - собрать только тесты buffer"
	@echo "  test-network       - собрать только тесты network"
	@echo "  run-test-buffer    - собрать и запустить тесты buffer"
	@echo "  run-test-network   - собрать и запустить тесты network"
	@echo "  test-integration   - собрать интеграционные тесты"
	@echo "  run-test-integration - собрать и запустить интеграционные тесты"
	@echo "  install-deps       - установить зависимости"
	@echo "  debug              - отладочная информация"

.PHONY: all test run-test run clean debug install-deps help
.PHONY: test-buffer test-network run-test-buffer run-test-network test-integration run-test-integration