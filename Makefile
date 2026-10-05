# Build and run the v_alloc tests.
#
#   make test     build and run the tests
#   make clean    remove build products
#
# Override the toolchain as usual, for example:
#   make test CC=clang CFLAGS="-std=c11 -O2 -Wall -Wextra"
#
# On Windows without make, use tests\run.bat instead.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O1 -g
BUILD   ?= build

PROGRAMS := $(BUILD)/test_v_alloc $(BUILD)/test_lifo_abort

.PHONY: test clean

test: $(PROGRAMS)
	@$(BUILD)/test_v_alloc
	@if $(BUILD)/test_lifo_abort >/dev/null 2>&1; then \
		echo "test_lifo_abort: no abort (asserts disabled) - skipped"; \
	else \
		echo "test_lifo_abort: aborted as expected"; \
	fi

$(BUILD)/test_v_alloc: tests/test_v_alloc.c v_alloc.c v_alloc.h | $(BUILD)
	$(CC) $(CFLAGS) -I. -o $@ tests/test_v_alloc.c v_alloc.c

$(BUILD)/test_lifo_abort: tests/test_lifo_abort.c v_alloc.c v_alloc.h | $(BUILD)
	$(CC) $(CFLAGS) -I. -o $@ tests/test_lifo_abort.c v_alloc.c

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
