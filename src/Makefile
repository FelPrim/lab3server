# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -std=c23 -O3 -march=native -flto -I.
LDFLAGS = -lpthread

# Папки
SRCDIR = .
TESTDIR = test
OBJDIR = obj
BINDIR = bin

# Автоматическое определение исходных файлов и заголовков
SOURCES = $(wildcard $(SRCDIR)/*.c)
HEADERS = $(wildcard $(SRCDIR)/*.h)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Автоматическое определение тестов
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c)
TEST_OBJECTS = $(TEST_SOURCES:$(TESTDIR)/%.c=$(OBJDIR)/test_%.o)

# Имена исполняемых файлов
TARGET = $(BINDIR)/server
TEST_RUNNER = $(BINDIR)/test_runner

# Автоматическая генерация зависимостей
DEPFILES = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.d) $(TEST_SOURCES:$(TESTDIR)/%.c=$(OBJDIR)/test_%.d)

# Основная цель
all: $(TARGET)

# Сборка основного сервера
$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Сборка тестового раннера
test: $(TEST_RUNNER)

$(TEST_RUNNER): $(TEST_OBJECTS) $(filter-out $(OBJDIR)/main.o, $(OBJECTS)) | $(BINDIR)
	$(CC) $(TEST_OBJECTS) $(filter-out $(OBJDIR)/main.o, $(OBJECTS)) -o $@ $(LDFLAGS)

# Компиляция объектных файлов с генерацией зависимостей
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Компиляция тестовых объектных файлов с генерацией зависимостей
$(OBJDIR)/test_%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

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
	@echo "Dependency files: $(DEPFILES)"
	@echo "Headers: $(HEADERS)"

# Установка зависимостей (для Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install libssl-dev

# Автоматические цели для отдельных тестов
TEST_TARGETS = $(patsubst $(TESTDIR)/%.c,$(BINDIR)/test_%,$(TEST_SOURCES))

$(BINDIR)/test_%: $(OBJDIR)/test_%.o $(filter-out $(OBJDIR)/main.o, $(OBJECTS)) | $(BINDIR)
	$(CC) $^ -o $@ $(LDFLAGS)

# Создание псевдоцелей для запуска отдельных тестов
define create_test_target
test-$(1): $(BINDIR)/test_$(1)
run-test-$(1): $(BINDIR)/test_$(1)
	./$(BINDIR)/test_$(1)
endef

# Автоматически создаем цели для всех тестов
TEST_NAMES = $(patsubst $(TESTDIR)/%.c,%,$(TEST_SOURCES))
$(foreach test,$(TEST_NAMES),$(eval $(call create_test_target,$(test))))

# Включение сгенерированных зависимостей
-include $(DEPFILES)

# Помощь
help:
	@echo "Доступные цели:"
	@echo "  all                - собрать основной сервер"
	@echo "  test               - собрать все тесты"
	@echo "  run-test           - собрать и запустить все тесты"
	@echo "  run                - собрать и запустить сервер"
	@echo "  clean              - очистить сборочные артефакты"
	@echo "  install-deps       - установить зависимости"
	@echo "  debug              - отладочная информация"
	@echo ""
	@echo "Автоматически сгенерированные цели для тестов:"
	@for test in $(TEST_NAMES); do \
		echo "  test-$$test        - собрать тест $$test"; \
		echo "  run-test-$$test    - собрать и запустить тест $$test"; \
	done

.PHONY: all test run-test run clean debug install-deps help
.PHONY: $(patsubst %,test-%,$(TEST_NAMES)) $(patsubst %,run-test-%,$(TEST_NAMES))