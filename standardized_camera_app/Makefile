CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc

TARGET := ov5640_capture
BUILD_DIR := build
SRC_DIR := src
INC_DIR := include

SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/v4l2_capture.c $(SRC_DIR)/camera_device.c $(SRC_DIR)/stream_http_mjpeg.c
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

CFLAGS := -Wall -Wextra -O2 -std=gnu99 -I$(INC_DIR)
LDFLAGS := -static

.PHONY: all clean

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@file $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
