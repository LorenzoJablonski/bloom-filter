CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O3 -Iinclude -MMD -MP
LDFLAGS = -lpthread -lm

SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
TARGET = bloom_app

# Filters
FILTERS = sbf cbf pbf rdbf hybrid
MODULES = murmurhash2 bf baseline keyset store benchmark iobench $(FILTERS) main
OBJS = $(MODULES:%=$(BUILD_DIR)/%.o)
TESTS = murmurhash2 $(FILTERS) baseline store

# Dataset generation (make data N=1000000 TYPES="uuid cpf")
N ?= 2000
TYPES ?= uuid cpf cnpj url
SEED ?= 42
PYTHON ?= python3

# Benchmark (make benchmark REPS=10 P=0.01)
REPS ?= 5
P ?= 0.01

.PHONY: all clean $(FILTERS) baseline data benchmark iobench demo repl test check

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Individual targets — compile a single module object
$(FILTERS) baseline: %: $(BUILD_DIR)/%.o

# Synthetic datasets + protocol partition (Dins / Dexist / Dinexist)
data:
	@for t in $(TYPES); do \
		$(PYTHON) scripts/gen_datasets.py --type $$t --count $(N) --seed $(SEED) && \
		$(PYTHON) scripts/partition.py --type $$t --seed $(SEED) || exit 1; \
	done

# Benchmark — one CSV per dataset type in results/ (long format, one row per rep)
benchmark: $(TARGET)
	@mkdir -p results
	@for t in $(TYPES); do \
		./$(TARGET) --benchmark --type $$t --p $(P) --reps $(REPS) --out results/benchmark_$$t.csv || exit 1; \
	done
	@echo "CSVs escritos em results/"

# I/O experiment (make iobench TABLES="1 8" NEG="0.5 0.9" QUERIES=10000 STORE_DIR=/mnt/hd/store)
# Tables are built once in STORE_DIR and reused; CACHED=1 uses the page cache instead of O_DIRECT
TABLES ?= 1 8
NEG ?= 0.5 0.9
QUERIES ?= 10000
VALUE_SIZE ?= 100
STORE_DIR ?= data/store
CACHED ?=

iobench: $(TARGET)
	@mkdir -p results
	@for t in $(TYPES); do for T in $(TABLES); do for r in $(NEG); do \
		./$(TARGET) --iobench --type $$t --tables $$T --neg $$r --queries $(QUERIES) \
			--reps $(REPS) --p $(P) --value-size $(VALUE_SIZE) --store-dir $(STORE_DIR) \
			$(if $(CACHED),--cached,) --out results/io_$${t}_T$${T}_neg$${r}.csv || exit 1; \
	done; done; done
	@echo "CSVs escritos em results/io_*.csv"

# Automatic demo — non-interactive, prints comparison table
demo: $(TARGET)
	./$(TARGET) --demo

# Interactive session — insert/search by hand
repl: $(TARGET)
	./$(TARGET) --repl

# Quick invariants through the generic interface (exit code non-zero on failure)
test: $(TARGET)
	./$(TARGET) --test

# C unit tests in tests/
check: $(OBJS)
	@for t in $(TESTS); do \
		obj=$(BUILD_DIR)/$$t.o; [ $$t = murmurhash2 ] && obj=; \
		$(CC) $(CFLAGS) $(TEST_DIR)/test_$$t.c $(BUILD_DIR)/murmurhash2.o $$obj \
			-o $(BUILD_DIR)/test_$$t $(LDFLAGS) && $(BUILD_DIR)/test_$$t || exit 1; \
	done

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(OBJS:.o=.d)
