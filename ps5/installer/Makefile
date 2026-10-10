# PS5SX2 Installer
#   make ps5    -> build/ps5/PS5SX2Installer.elf   (payload for the console's ELF loader)
#   make host   -> build/host/ps5sx2-installer     (Linux build for the tests; works under $PS5SX2_ROOT)
#   make test   -> host builds (both variants) + tests/run-tests.sh
#
# NOLOG=1 builds the no-log installer: it installs and leaves. No logger, no report code and no relay address in
# it (build/ps5-nolog/PS5SX2Installer-nolog.elf, build/host-nolog/ps5sx2-installer-nolog).
#
# Dependencies (see fetch-deps.sh): Mbed TLS 3.6.7 and miniz 3.1.1 in $(DEPS); the PS5 payload SDK in
# $(PS5_PAYLOAD_SDK). RELAY_URL is the log relay built into the PS5 ELF (the Cloudflare Worker in worker/,
# deployed on Spyros's account 2026-09-28); `make ps5 RELAY_URL=` builds an ELF that only keeps reports in
# outbox/. Host builds never have one built in: the tests pass theirs in PS5SX2_TEST_RELAY_URL.

PS5_PAYLOAD_SDK ?= /home/claude/ps5deps/sdk285
DEPS ?= /home/claude/ps5deps/installer
RELAY_URL ?= https://ps5sx2-logs.ps5sx2.workers.dev/v1/logs
NOLOG ?=

MBEDTLS := $(DEPS)/mbedtls-3.6.7
MINIZ := $(DEPS)/miniz-3.1.1

SRC_COMMON := src/util.c src/paths.c src/log.c src/fsx.c src/net.c src/tls.c src/http.c src/json.c \
              src/github.c src/zipx.c src/app.c src/install.c src/main.c
MBED_SRC := $(wildcard $(MBEDTLS)/library/*.c)

DEFS := -DMBEDTLS_USER_CONFIG_FILE='"mbedtls_user_config.h"'
ifeq ($(NOLOG),1)
VARIANT := -nolog
override RELAY_URL :=
DEFS += -DINSTALLER_NO_LOGGER
else
VARIANT :=
SRC_COMMON += src/report.c src/logger.c
endif

INCS := -Isrc -Iconfig -I$(MBEDTLS)/include -I$(MINIZ)
MINIZ_DEFS := -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
WARN := -Wall -Wextra -Werror -Wno-unused-parameter

# ---------------- PS5 ----------------
PS5_CC := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
PS5_AR := $(PS5_PAYLOAD_SDK)/bin/prospero-ar
PS5_OUT := build/ps5$(VARIANT)
PS5_CFLAGS := -std=gnu11 -O2 -fPIC $(DEFS) $(INCS) -DRELAY_URL_DEFAULT='"$(RELAY_URL)"'

PS5_OBJ := $(patsubst src/%.c,$(PS5_OUT)/%.o,$(SRC_COMMON) src/plat_ps5.c) $(PS5_OUT)/cacert_data.o $(PS5_OUT)/history_data.o
PS5_MBED_OBJ := $(patsubst $(MBEDTLS)/library/%.c,$(PS5_OUT)/mbedtls/%.o,$(MBED_SRC))

ps5: $(PS5_OUT)/PS5SX2Installer$(VARIANT).elf

$(PS5_OUT)/%.o: src/%.c src/*.h config/mbedtls_user_config.h | $(PS5_OUT)
	$(PS5_CC) $(PS5_CFLAGS) $(WARN) -c -o $@ $<

# logger.o (the only user of RELAY_URL_DEFAULT) is rebuilt whenever RELAY_URL changes
$(PS5_OUT)/relay-url.txt: FORCE | $(PS5_OUT)
	@printf '%s\n' '$(RELAY_URL)' | cmp -s - $@ || printf '%s\n' '$(RELAY_URL)' > $@
$(PS5_OUT)/logger.o: $(PS5_OUT)/relay-url.txt

$(PS5_OUT)/cacert_data.o: $(PS5_OUT)/cacert_data.c
	$(PS5_CC) $(PS5_CFLAGS) -c -o $@ $<

$(PS5_OUT)/history_data.o: $(PS5_OUT)/history_data.c src/history.h
	$(PS5_CC) $(PS5_CFLAGS) $(WARN) -c -o $@ $<

$(PS5_OUT)/mbedtls/%.o: $(MBEDTLS)/library/%.c config/mbedtls_user_config.h | $(PS5_OUT)/mbedtls
	$(PS5_CC) $(PS5_CFLAGS) -I$(MBEDTLS)/library -c -o $@ $<

$(PS5_OUT)/miniz.o: $(MINIZ)/miniz.c | $(PS5_OUT)
	$(PS5_CC) $(PS5_CFLAGS) $(MINIZ_DEFS) -Wno-unused-function -c -o $@ $<

$(PS5_OUT)/libmbedtls_all.a: $(PS5_MBED_OBJ)
	rm -f $@ && $(PS5_AR) rcs $@ $^

$(PS5_OUT)/PS5SX2Installer$(VARIANT).elf: $(PS5_OBJ) $(PS5_OUT)/miniz.o $(PS5_OUT)/libmbedtls_all.a
	$(PS5_CC) -o $@ $(PS5_OBJ) $(PS5_OUT)/miniz.o $(PS5_OUT)/libmbedtls_all.a -lSceNet

# ---------------- host ----------------
HOST_CC ?= cc
HOST_OUT := build/host$(VARIANT)
HOST_SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer
HOST_CFLAGS := -std=gnu11 -O1 -g -D_GNU_SOURCE -DPS5SX2_HOST $(DEFS) $(INCS) -DRELAY_URL_DEFAULT='""'

HOST_OBJ := $(patsubst src/%.c,$(HOST_OUT)/%.o,$(SRC_COMMON) src/plat_host.c) $(HOST_OUT)/cacert_data.o $(HOST_OUT)/history_data.o
HOST_MBED_OBJ := $(patsubst $(MBEDTLS)/library/%.c,$(HOST_OUT)/mbedtls/%.o,$(MBED_SRC))

host: $(HOST_OUT)/ps5sx2-installer$(VARIANT)

$(HOST_OUT)/%.o: src/%.c src/*.h config/mbedtls_user_config.h | $(HOST_OUT)
	$(HOST_CC) $(HOST_CFLAGS) $(HOST_SAN) $(WARN) -c -o $@ $<

$(HOST_OUT)/cacert_data.o: $(HOST_OUT)/cacert_data.c
	$(HOST_CC) $(HOST_CFLAGS) -c -o $@ $<

$(HOST_OUT)/history_data.o: $(HOST_OUT)/history_data.c src/history.h
	$(HOST_CC) $(HOST_CFLAGS) $(WARN) -c -o $@ $<

$(HOST_OUT)/mbedtls/%.o: $(MBEDTLS)/library/%.c config/mbedtls_user_config.h | $(HOST_OUT)/mbedtls
	$(HOST_CC) $(HOST_CFLAGS) -I$(MBEDTLS)/library -c -o $@ $<

$(HOST_OUT)/miniz.o: $(MINIZ)/miniz.c | $(HOST_OUT)
	$(HOST_CC) $(HOST_CFLAGS) $(MINIZ_DEFS) $(HOST_SAN) -Wno-unused-function -c -o $@ $<

$(HOST_OUT)/libmbedtls_all.a: $(HOST_MBED_OBJ)
	rm -f $@ && ar rcs $@ $^

$(HOST_OUT)/ps5sx2-installer$(VARIANT): $(HOST_OBJ) $(HOST_OUT)/miniz.o $(HOST_OUT)/libmbedtls_all.a
	$(HOST_CC) $(HOST_SAN) -o $@ $(HOST_OBJ) $(HOST_OUT)/miniz.o $(HOST_OUT)/libmbedtls_all.a

# ---------------- shared ----------------
%/cacert_data.c: ca/cacert.pem tools/embed.py | %
	python3 tools/embed.py ca/cacert.pem $@

%/history_data.c: history/*.json tools/history.py | %
	python3 tools/history.py $@ history

$(PS5_OUT) $(PS5_OUT)/mbedtls $(HOST_OUT) $(HOST_OUT)/mbedtls:
	mkdir -p $@

test: host
	$(MAKE) host NOLOG=1
	bash tests/run-tests.sh

clean:
	rm -rf build

.PHONY: ps5 host test clean FORCE
FORCE:
.SECONDARY:
