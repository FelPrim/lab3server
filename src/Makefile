# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -pedantic -O3 -g
LDFLAGS = -lssl -lcrypto -luthash

# Целевой исполняемый файл
TARGET = server

# Исходные файлы
SRCS = server.c protocol.c network_setup.c connection_manager.c call_manager.c

# Объектные файлы
OBJS = $(SRCS:.c=.o)

# Заголовочные файлы
HEADERS = protocol.h useful_stuff.h nonblocking_ssl.h network_setup.h connection_manager.h call_manager.h

# Правило по умолчанию
all: $(TARGET)

# Сборка исполняемого файла
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Компиляция объектных файлов
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Очистка
clean:
	rm -f $(OBJS) $(TARGET)

# Пересборка
rebuild: clean all

# Установка зависимостей (для Debian/Ubuntu)
install-deps:
	sudo apt-get update
	sudo apt-get install libssl-dev libuthash-dev

# Запуск сервера
run: $(TARGET)
	./$(TARGET)

# Отладка
debug: CFLAGS += -DDEBUG -g3
debug: rebuild

# Профилирование  
profile: CFLAGS += -pg
profile: LDFLAGS += -pg
profile: rebuild

# Статический анализ
analyze: clean
	scan-build make

# Информация о проекте
info:
	@echo "=== Server Project Information ==="
	@echo "Target: $(TARGET)"
	@echo "Source files: $(SRCS)"
	@echo "Header files: $(HEADERS)"
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"
	@echo "Libraries: $(LDFLAGS)"

.PHONY: all clean rebuild install-deps run debug profile analyze info
