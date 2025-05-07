# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c99 -g

# Директории
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

# Исходники и объектные файлы
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Итоговый исполняемый файл
TARGET = $(BIN_DIR)/lz77

# Основное правило
all: $(TARGET)

# Сборка исполняемого файла
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

# Компиляция .c в .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Очистка
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Установка в /usr/local/bin
install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/lz77
	sudo chmod +x /usr/local/bin/lz77
	@echo "Утилита lz77 установлена. Запустите 'lz77 -h' для проверки."

# Удаление установленной утилиты
uninstall:
	sudo rm -f /usr/local/bin/lz77
	@echo "Утилита lz77 удалена."

.PHONY: all clean install uninstall