CC = gcc
CFLAGS = -O2 -Wall
LDFLAGS=-lcrypto

OBJ_DIR = obj
SRC_DIR = src
INC_DIR = include

INC=-$(INC_DIR)

TARGET = lp25-backup

all: $(TARGET)


$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(INC_DIR)/%.h
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(OBJ_DIR)/file-properties.o:  $(SRC_DIR)/file-properties.c $(INC_DIR)/file-properties.h
	$(CC) $(CFLAGS) -std=c11 $(INC) -c $< -o $@


$(TARGET): $(SRC_DIR)/main.c $(OBJ_DIR)/%.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC) -o $@ $^


clean : 
	rm -f $(OBJ_DIR)/* $(TARGET)