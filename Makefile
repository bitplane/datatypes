PACKAGE ?= targa
FORMATS := $(notdir $(wildcard formats/*))
TESTFLAGS = -std=c99 -I. -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
# Shared modules that also build on the host, with the host libraries they need.
# A format's test links one when a file in the format includes its header.
HOST_COMMON := zlib bcn bptc png
HOST_LIBS_zlib := -lz
host_common = $(foreach m,$(HOST_COMMON),$(if $(shell grep -ls '"common/$(m).h"' formats/$(1)/*.[ch]),common/$(m).c $(HOST_LIBS_$(m))))

.PHONY: test build release

test: $(FORMATS:%=test-%)

test-%:
	mkdir -p build
	$(CC) $(TESTFLAGS) tests/$*.c $(filter-out %class.c,$(wildcard formats/$*/*.c)) $(call host_common,$*) -o build/$*-test
	ASAN_OPTIONS=detect_leaks=0 build/$*-test

build:
	sh scripts/build.sh $(TARGET) $(PACKAGE)

release:
	sh scripts/release.sh $(PACKAGE) $(LEVEL) $(DRY_RUN)
