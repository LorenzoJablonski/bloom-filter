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

# Benchmark / I/O parameters
REPS ?= 5
P ?= 0.01

.PHONY: all clean $(FILTERS) baseline data benchmark iobench plots venv demo repl test check

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

# Experiments run through scripts/run.py: every bloom_app invocation is recorded in
# results/bloom.db (code, machine, device, dataset, parameters) and its raw CSV kept
# in results/raw/. REPEAT runs the whole grid again; LABEL names the campaign;
# ALLOW_DIRTY=1 permits uncommitted tracked changes (those runs are flagged).
REPEAT ?= 1
LABEL ?=
ALLOW_DIRTY ?=
RUN_FLAGS = --types $(TYPES) --p $(P) --reps $(REPS) --repeat $(REPEAT) \
	$(if $(LABEL),--label $(LABEL),) $(if $(ALLOW_DIRTY),--allow-dirty,)

# In-memory benchmark (make benchmark REPS=10 REPEAT=3 LABEL=ssd-100k)
benchmark: $(TARGET)
	$(PYTHON) scripts/run.py benchmark $(RUN_FLAGS)

# I/O experiment (make iobench TABLES="1 8" NEG="0.5 0.9" QUERIES=10000 STORE_DIR=/mnt/hd/store)
# Tables are built once in STORE_DIR and reused; CACHED=1 uses the page cache instead of O_DIRECT
TABLES ?= 1 8
NEG ?= 0.5 0.9
QUERIES ?= 10000
VALUE_SIZE ?= 100
STORE_DIR ?= data/store
CACHED ?=

iobench: $(TARGET)
	$(PYTHON) scripts/run.py iobench $(RUN_FLAGS) --tables $(TABLES) --neg $(NEG) \
		--queries $(QUERIES) --value-size $(VALUE_SIZE) --store-dir $(STORE_DIR) \
		$(if $(CACHED),--cached,)

# Figures from results/bloom.db (make plots FORMAT="pdf png" PLOT_ARGS="--label hd-T8")
# Needs matplotlib in $(VENV): run `make venv` once. A missing experiment is skipped.
VENV ?= .venv
PLOT_DIR ?= results/plots
FORMAT ?= pdf
PLOT_ARGS ?=
DB ?= results/bloom.db

venv: $(VENV)/.installed

# stamp file: .venv/bin/python is a symlink to the system interpreter
$(VENV)/.installed: requirements.txt
	@if command -v uv >/dev/null 2>&1; then \
		uv venv --allow-existing $(VENV) && VIRTUAL_ENV=$(VENV) uv pip install -r requirements.txt; \
	else \
		$(PYTHON) -m venv $(VENV) && $(VENV)/bin/pip install -r requirements.txt; \
	fi
	@touch $@

plots: $(VENV)/.installed
	-$(VENV)/bin/python scripts/analyze.py --db $(DB) bench --plot $(PLOT_DIR) --format $(FORMAT) $(PLOT_ARGS)
	-$(VENV)/bin/python scripts/analyze.py --db $(DB) io --plot $(PLOT_DIR) --format $(FORMAT) $(PLOT_ARGS)

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
