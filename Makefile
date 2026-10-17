CC = gcc
PYTHON ?= python3
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Ieditor
CFLAGS ?= -std=c99 -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wconversion -Wstrict-prototypes -Wmissing-prototypes
LDFLAGS ?=
LDLIBS ?=
ASAN_OPTIONS ?= detect_leaks=1:allocator_may_return_null=1
CORE = editor/image.c editor/parse.c editor/netpbm.c editor/operations.c
SOURCES = image_editor.c editor/commands.c $(CORE)
FLAGS_debug = -g3 -Og
FLAGS_release = -O2
FLAGS_asan = -g3 -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
WRAPS = malloc fclose fwrite rename mkstemp fdopen fflush fsync fchmod
WRAP_FLAGS = $(foreach fn,$(WRAPS),-Wl,--wrap=$(fn))
VALGRIND_TESTS = EditorTests.test_eof_and_exit_are_clean EditorTests.test_fault_injection EditorTests.test_invalid_numeric_commands_do_not_mutate EditorTests.test_rejected_rectangle_does_not_change_pixels EditorTests.test_save_open_and_buffered_write_failures

.PHONY: build debug release sanitize test test-release test-sanitize test-valgrind test-assignment analyze clean
build: image_editor
debug: image_editor
release: build/release/image_editor
sanitize: build/asan/image_editor

image_editor: build/debug/image_editor
	cp $< $@

# Separate objects prevent mixing optimization and sanitizer settings.
define MODE_RULES
build/$(1)/%.o: %.c
	mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) $$(FLAGS_$(1)) -MMD -MP -c $$< -o $$@

build/$(1)/image_editor: $$(patsubst %.c,build/$(1)/%.o,$$(SOURCES))
	$$(CC) $$(LDFLAGS) $$(FLAGS_$(1)) $$^ $$(LDLIBS) -o $$@

build/$(1)/fault-injection: build/$(1)/tests/fault_injection.o $$(patsubst %.c,build/$(1)/%.o,$$(CORE) editor/commands.c)
	$$(CC) $$(LDFLAGS) $$(FLAGS_$(1)) $$(WRAP_FLAGS) $$^ $$(LDLIBS) -o $$@
endef
$(foreach mode,debug release asan,$(eval $(call MODE_RULES,$(mode))))

-include $(foreach mode,debug release asan,$(patsubst %.c,build/$(mode)/%.d,$(SOURCES) tests/fault_injection.c))

test: image_editor build/debug/fault-injection
	$(PYTHON) -B tests/test_editor.py --binary ./build/debug/image_editor --fault-binary ./build/debug/fault-injection

test-release: build/release/image_editor build/release/fault-injection
	$(PYTHON) -B tests/test_editor.py --binary ./build/release/image_editor --fault-binary ./build/release/fault-injection

test-sanitize: build/asan/image_editor build/asan/fault-injection
	ASAN_OPTIONS=$(ASAN_OPTIONS) $(PYTHON) -B tests/test_editor.py --binary ./build/asan/image_editor --fault-binary ./build/asan/fault-injection

test-valgrind: build/debug/image_editor build/debug/fault-injection
	$(PYTHON) -B tests/test_editor.py --binary ./build/debug/image_editor --fault-binary ./build/debug/fault-injection --valgrind $(VALGRIND_TESTS)

test-assignment: image_editor
	$(PYTHON) -B tests/run_assignment.py --binary ./image_editor

analyze: $(patsubst %.c,build/analysis/%.o,$(SOURCES))

build/analysis/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror -fanalyzer -MMD -MP -c $< -o $@

-include $(patsubst %.c,build/analysis/%.d,$(SOURCES))

.PHONY: benchmark check-export pack
benchmark:
	$(PYTHON) -B tools/benchmark.py

check-export:
	$(PYTHON) -B tools/check_export.py

pack:
	$(PYTHON) -B tools/export_source.py --destination build/source-package

# Keep historical outputs and baseline snapshots outside the current build dirs.
clean:
	rm -rf build/debug build/release build/asan build/analysis
	rm -f image_editor
