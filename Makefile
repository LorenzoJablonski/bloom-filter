CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O3 -Iinclude
LDFLAGS = -lpthread

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
BUILD_DIR = build
TARGET = bloom_app

# Source files
COMMON_SRC = $(SRC_DIR)/murmurhash2.c
BASELINE_SRC = $(SRC_DIR)/baseline.c
FILTER_SRC = $(SRC_DIR)/sbf.c $(SRC_DIR)/cbf.c $(SRC_DIR)/pbf.c $(SRC_DIR)/rdbf.c $(SRC_DIR)/hybrid.c
MAIN_SRC = $(SRC_DIR)/main.c

# Object files
COMMON_OBJ = $(BUILD_DIR)/murmurhash2.o
BASELINE_OBJ = $(BUILD_DIR)/baseline.o
FILTER_OBJ = $(BUILD_DIR)/sbf.o $(BUILD_DIR)/cbf.o $(BUILD_DIR)/pbf.o $(BUILD_DIR)/rdbf.o $(BUILD_DIR)/hybrid.o
MAIN_OBJ = $(BUILD_DIR)/main.o

.PHONY: all clean sbf cbf pbf rdbf hybrid baseline benchmark test

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile common
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile filters
$(BUILD_DIR)/sbf.o: $(SRC_DIR)/sbf.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cbf.o: $(SRC_DIR)/cbf.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/pbf.o: $(SRC_DIR)/pbf.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rdbf.o: $(SRC_DIR)/rdbf.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hybrid.o: $(SRC_DIR)/hybrid.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/baseline.o: $(SRC_DIR)/baseline.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link
$(TARGET): $(COMMON_OBJ) $(BASELINE_OBJ) $(FILTER_OBJ) $(MAIN_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Individual targets
sbf: $(BUILD_DIR) $(COMMON_OBJ) $(BUILD_DIR)/sbf.o
	$(CC) $(CFLAGS) $(COMMON_OBJ) $(BUILD_DIR)/sbf.o -o $(BUILD_DIR)/$@ $(LDFLAGS)

cbf: $(BUILD_DIR) $(COMMON_OBJ) $(BUILD_DIR)/cbf.o
	$(CC) $(CFLAGS) $(COMMON_OBJ) $(BUILD_DIR)/cbf.o -o $(BUILD_DIR)/$@ $(LDFLAGS)

pbf: $(BUILD_DIR) $(COMMON_OBJ) $(BUILD_DIR)/pbf.o
	$(CC) $(CFLAGS) $(COMMON_OBJ) $(BUILD_DIR)/pbf.o -o $(BUILD_DIR)/$@ $(LDFLAGS)

rdbf: $(BUILD_DIR) $(COMMON_OBJ) $(BUILD_DIR)/rdbf.o
	$(CC) $(CFLAGS) $(COMMON_OBJ) $(BUILD_DIR)/rdbf.o -o $(BUILD_DIR)/$@ $(LDFLAGS)

hybrid: $(BUILD_DIR) $(COMMON_OBJ) $(BUILD_DIR)/hybrid.o
	$(CC) $(CFLAGS) $(COMMON_OBJ) $(BUILD_DIR)/hybrid.o -o $(BUILD_DIR)/$@ $(LDFLAGS)

baseline: $(BUILD_DIR) $(COMMON_OBJ) $(BUILD_DIR)/baseline.o
	$(CC) $(CFLAGS) $(COMMON_OBJ) $(BUILD_DIR)/baseline.o -o $(BUILD_DIR)/$@ $(LDFLAGS)

# Benchmark
benchmark: $(TARGET)
	./$(TARGET) --benchmark

# Test
test: $(TARGET)
	./$(TARGET) --test

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
