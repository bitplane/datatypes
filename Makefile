PACKAGE ?= targa

.PHONY: test build release

test:
	mkdir -p build
	$(CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/targa.c formats/targa/decode.c formats/targa/encode.c -o build/targa-test
	ASAN_OPTIONS=detect_leaks=0 build/targa-test
	$(CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/pcx.c formats/pcx/decode.c formats/pcx/encode.c -o build/pcx-test
	ASAN_OPTIONS=detect_leaks=0 build/pcx-test
	$(CC) -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer tests/qoi.c formats/qoi/decode.c formats/qoi/encode.c -o build/qoi-test
	ASAN_OPTIONS=detect_leaks=0 build/qoi-test

build:
	sh scripts/build.sh $(TARGET) $(PACKAGE)

release:
	sh scripts/release.sh $(PACKAGE) $(LEVEL) $(DRY_RUN)
