CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc
HOST_CC ?= gcc

TARGET := ov5640_capture
BUILD_DIR := build
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests

SRCS := $(SRC_DIR)/main.c $(SRC_DIR)/v4l2_capture.c $(SRC_DIR)/camera_device.c $(SRC_DIR)/stream_http_mjpeg.c $(SRC_DIR)/frame_queue.c $(SRC_DIR)/framebuffer_display.c
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
FRAME_QUEUE_TEST := $(BUILD_DIR)/frame_queue_test

CFLAGS := -Wall -Wextra -O2 -std=gnu99 -I$(INC_DIR) -pthread
LDFLAGS := -static -pthread

.PHONY: all clean test-frame-queue

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@file $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(FRAME_QUEUE_TEST): $(TEST_DIR)/frame_queue_test.c $(SRC_DIR)/frame_queue.c | $(BUILD_DIR)
	$(HOST_CC) $(CFLAGS) -pthread -o $@ $^

test-frame-queue: $(FRAME_QUEUE_TEST)
	./$(FRAME_QUEUE_TEST)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
