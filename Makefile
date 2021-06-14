ifeq ($(shell uname),Darwin)
    LDFLAGS := -Wl,-dead_strip -framework Security -framework Foundation
else
    LDFLAGS := -Wl,--gc-sections -lpthread -ldl

endif

PROFILE := debug
DESTDIR=/usr/local

CFLAGS := -Werror -Wall -Wextra -Wpedantic -g -I src/

ifeq ($(CC), clang)
	CFLAGS += -fsanitize=address -fsanitize=undefined
	LDFLAGS += -fsanitize=address
endif

ifeq ($(PROFILE), release)
	CFLAGS += -O3
	CARGOFLAGS += --release
endif

all: target/crustls-demo

test: all
	cargo test
	target/client httpbin.org 443 /headers

target:
	mkdir -p $@

src/crustls.h: src/*.rs cbindgen.toml
	cargo check
	cbindgen --lang C > $@

target/$(PROFILE)/libcrustls.a: src/*.rs Cargo.toml
	cargo build $(CARGOFLAGS)

target/%.o: tests/%.c src/crustls.h tests/common.h
	$(CC) -o $@ -c $< $(CFLAGS)

target/client: target/client.o target/common.o target/$(PROFILE)/libcrustls.a
	$(CC) -o $@ $^ $(LDFLAGS)

target/server: target/server.o target/common.o target/$(PROFILE)/libcrustls.a
	$(CC) -o $@ $^ $(LDFLAGS)

install: target/$(PROFILE)/libcrustls.a src/crustls.h
	mkdir -p $(DESTDIR)/lib
	install target/$(PROFILE)/libcrustls.a $(DESTDIR)/lib/
	mkdir -p $(DESTDIR)/include
	install src/crustls.h $(DESTDIR)/include/crustls.h

clean:
	rm -rf target
