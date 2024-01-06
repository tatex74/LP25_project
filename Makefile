CC = gcc
INCLUDE = -Iinclude
CFLAGS = -Wall -lssl -lcrypto

OBJ_DIR = obj
SRC_DIR = src

SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC_FILES))

TARGET = LP25_sync

$(TARGET): $(OBJ_FILES) 
	$(CC) $(INCLUDE) -o $@ $^ $(CFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(INCLUDE) -c $< -o $@ $(CFLAGS)

clean : 
	rm $(OBJ_DIR)/* $(TARGET)