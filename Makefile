CC ?= cc
AR ?= ar
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
SYSCONFDIR ?= $(PREFIX)/etc
DESTDIR ?=
CFLAGS ?= -O2 -g
# Avoid contraction: the historical voice's floating-point evaluation is audible.
CFLAGS += -std=c11 -Wall -Wextra -ffp-contract=off -fsigned-char
SPEECHD_CFLAGS ?= -I$(shell $(PKG_CONFIG) --variable=includedir speech-dispatcher)
SPEECHD_LIBS ?= -L$(shell $(PKG_CONFIG) --variable=libdir speech-dispatcher) -lspeechd_module
XML_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libxml-2.0)
XML_LIBS ?= $(shell $(PKG_CONFIG) --libs libxml-2.0)
.PHONY: all core test install clean
all: build/sd_synthe3_ng build/libsynthe3.a
# Portable core: no Speech Dispatcher or XML headers/libraries.
core: build/libsynthe3.a
build:
	mkdir -p $@
build/engine.o: src/engine.c src/engine.h src/voice_data.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/dsp.o: src/dsp.c src/dsp.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/text.o: src/text.c src/text.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
build/module.o: src/module.c src/engine.h src/dsp.h src/text.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SPEECHD_CFLAGS) $(XML_CFLAGS) -Isrc/compat -c $< -o $@
build/libsynthe3.a: build/engine.o build/dsp.o build/text.o
	$(AR) rcs $@ $^
build/sd_synthe3_ng: build/module.o build/engine.o build/dsp.o build/text.o
	$(CC) $^ $(LDFLAGS) $(SPEECHD_LIBS) $(XML_LIBS) -lm -o $@
# Historical PCM oracle: retain the pre-adjustment defaults for fingerprint tests.
build/sd_synthe3_ng_historical: src/module.c src/engine.c src/dsp.c src/text.c src/engine.h src/dsp.h src/text.h src/voice_data.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SPEECHD_CFLAGS) $(XML_CFLAGS) -Isrc/compat -DS3_OUTPUT_GAIN_DB=0 -DS3_PITCH_BASE=6 src/module.c src/engine.c src/dsp.c src/text.c $(LDFLAGS) $(SPEECHD_LIBS) $(XML_LIBS) -lm -o $@
build/check_text: tests/check_text.c build/libsynthe3.a src/text.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $< build/libsynthe3.a $(LDFLAGS) -o $@
.PHONY: test-core
test-core: build/check_text
	./build/check_text
test: all build/sd_synthe3_ng_historical test-core test-api
	python3 -m unittest discover -s tests -v
install: all
	install -Dm755 build/sd_synthe3_ng $(DESTDIR)$(PREFIX)/libexec/synthe3-ng/sd_synthe3_ng
	install -Dm644 config/synthe3_ng.conf $(DESTDIR)$(SYSCONFDIR)/speech-dispatcher/modules/synthe3_ng.conf
	install -d $(DESTDIR)$(PREFIX)/share/licenses/synthe3-ng $(DESTDIR)$(PREFIX)/share/doc/synthe3-ng
	install -m644 upstream/LICENSE-* LICENSE.md $(DESTDIR)$(PREFIX)/share/licenses/synthe3-ng/
	install -m644 README.md UPSTREAM.md $(DESTDIR)$(PREFIX)/share/doc/synthe3-ng/
	cp -r docs $(DESTDIR)$(PREFIX)/share/doc/synthe3-ng/
clean:
	rm -rf build
.PHONY: sanitize
sanitize: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -fsanitize=address,undefined,float-cast-overflow -Isrc tests/fuzz_engine.c src/engine.c src/dsp.c -lm -o build/fuzz_engine
	./build/fuzz_engine
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -fsanitize=address,undefined -Isrc tests/check_text.c src/text.c -o build/check_text_sanitize
	./build/check_text_sanitize
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -fsanitize=address,undefined -Isrc tests/check_api.c src/api.c src/engine.c src/dsp.c -lm -pthread -o build/check_api_sanitize
	./build/check_api_sanitize
# Requires SPEECHD_ADDRESS pointing to an isolated test server. Silent playback.
SPEECHD_CLIENT_LIBS ?= -L$(shell $(PKG_CONFIG) --variable=libdir speech-dispatcher) -lspeechd
build/check_speechd: tests/check_speechd.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SPEECHD_CFLAGS) $< $(LDFLAGS) $(SPEECHD_CLIENT_LIBS) -pthread -o $@

# DLL and native shared library use separate outputs: never mix target objects.
API_SOURCES = src/api.c src/engine.c src/dsp.c
API_HEADERS = src/api.h src/engine.h src/dsp.h src/text.h src/voice_data.h
.PHONY: api windows
api: build/libsynthe3.so build/render_api build/check_api
build/libsynthe3.so: $(API_SOURCES) $(API_HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DS3_API_BUILD -fPIC -fvisibility=hidden -shared $(API_SOURCES) $(LDFLAGS) -lm -o $@
WINDOWS_CC ?= x86_64-w64-mingw32-gcc
WINDOWS_LDFLAGS ?= -static -static-libgcc
windows: build/windows-x64/synthe3.dll build/windows-x64/render_api.exe build/windows-x64/check_api.exe
build/windows-x64:
	mkdir -p $@
build/windows-x64/synthe3.dll build/windows-x64/libsynthe3.dll.a &: $(API_SOURCES) $(API_HEADERS) | build/windows-x64
	$(WINDOWS_CC) $(CPPFLAGS) $(CFLAGS) -DS3_API_BUILD -shared $(API_SOURCES) $(LDFLAGS) $(WINDOWS_LDFLAGS) -Wl,--exclude-all-symbols,--out-implib,build/windows-x64/libsynthe3.dll.a -lm -o build/windows-x64/synthe3.dll

build/render_api: tools/render_api.c src/text.c src/text.h src/api.h build/libsynthe3.so
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc tools/render_api.c src/text.c -Lbuild -lsynthe3 -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) -o $@
build/check_api: tests/check_api.c src/api.h build/libsynthe3.so
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc $< -Lbuild -lsynthe3 -Wl,-rpath,'$$ORIGIN' $(LDFLAGS) -pthread -o $@
build/windows-x64/render_api.exe: tools/render_api.c src/text.c src/text.h src/api.h build/windows-x64/synthe3.dll build/windows-x64/libsynthe3.dll.a
	$(WINDOWS_CC) $(CPPFLAGS) $(CFLAGS) -Isrc tools/render_api.c src/text.c build/windows-x64/libsynthe3.dll.a $(LDFLAGS) $(WINDOWS_LDFLAGS) -o $@
build/windows-x64/check_api.exe: tests/check_api.c src/api.h build/windows-x64/synthe3.dll build/windows-x64/libsynthe3.dll.a
	$(WINDOWS_CC) $(CPPFLAGS) $(CFLAGS) -Isrc $< build/windows-x64/libsynthe3.dll.a $(LDFLAGS) $(WINDOWS_LDFLAGS) -o $@
.PHONY: test-api
test-api: api
	./build/check_api
