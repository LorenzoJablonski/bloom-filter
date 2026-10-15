CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O3 -Iinclude
LDFLAGS = -lpthread -lm

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
BUILD_DIR = build
TARGET = bloom_app

# Source files — add filters here as they are implemented (cbf, pbf, rdbf, hybrid)
COMMON_SRC = $(SRC_DIR)/murmurhash2.c
BASELINE_SRC = $(SRC_DIR)/baseline.c
FILTER_SRC = $(SRC_DIR)/sbf.c
MAIN_SRC = $(SRC_DIR)/main.c

# Object files
COMMON_OBJ = $(BUILD_DIR)/murmurhash2.o
BASELINE_OBJ = $(BUILD_DIR)/baseline.o
FILTER_OBJ = $(BUILD_DIR)/sbf.o
MAIN_OBJ = $(BUILD_DIR)/main.o

.PHONY: all clean sbf cbf pbf rdbf hybrid baseline benchmark demo repl test

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

# Automatic demo — non-interactive, prints comparison table
demo: $(TARGET)
	./$(TARGET) --demo

# Interactive session — insert/search by hand
repl: $(TARGET)
	./$(TARGET) --repl

# Test — quick invariants (exit code non-zero on failure)
test: $(TARGET)
	./$(TARGET) --test

# Compile and run the C unit tests in tests/
check: $(BUILD_DIR)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_sbf.c $(COMMON_OBJ) $(BUILD_DIR)/sbf.o -o $(BUILD_DIR)/test_sbf $(LDFLAGS)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_baseline.c $(COMMON_OBJ) $(BUILD_DIR)/baseline.o -o $(BUILD_DIR)/test_baseline $(LDFLAGS)
	$(BUILD_DIR)/test_sbf
	$(BUILD_DIR)/test_baseline

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
