PACKAGE ?= targa
FORMATS := $(notdir $(wildcard formats/*))
TESTFLAGS = -std=c99 -I. -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test build release

test: $(FORMATS:%=test-%)

test-%:
	mkdir -p build
	$(CC) $(TESTFLAGS) tests/$*.c $(filter-out %class.c,$(wildcard formats/$*/*.c)) -o build/$*-test
	ASAN_OPTIONS=detect_leaks=0 build/$*-test

build:
	sh scripts/build.sh $(TARGET) $(PACKAGE)

release:
	sh scripts/release.sh $(PACKAGE) $(LEVEL) $(DRY_RUN)
