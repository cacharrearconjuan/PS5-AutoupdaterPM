ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK no está definido)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

# Versión que se graba dentro del ELF. GitHub Actions la pasa con: make VERSION=1.2.0
VERSION ?= dev

ELF       := PS5-AutoupdaterPM.elf
OBJS      := main.o
CA_PEM    ?= cacert.pem
CA_URL    := https://curl.se/ca/cacert.pem

# Copia suelta que se usa si no está instalado desde Payload Manager
UPDATER_PATH := /data/pldmgr/PS5-AutoupdaterPM.elf

HEADERS := cacert_pem.h

CFLAGS  := -Os -Wall -ffunction-sections -fdata-sections \
           -I$(PS5_PAYLOAD_SDK)/target/include \
           -DAPP_VERSION=\"$(VERSION)\" -DUPDATER_PATH=\"$(UPDATER_PATH)\"
LDFLAGS := -L$(PS5_PAYLOAD_SDK)/target/lib -Wl,--gc-sections
# Solo las mismas librerías del sistema que la v1.1.0
LDLIBS  := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lpthread

.PHONY: all clean FORCE

all: $(ELF)

# Recompila main.o cuando cambia VERSION
.version: FORCE
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@

$(CA_PEM):
	curl -fsSL -o $@ $(CA_URL)

cacert_pem.h: $(CA_PEM) tools/bin2h.py
	python3 tools/bin2h.py $< $@ cacert_pem

main.o: main.c pm_slots.h $(HEADERS) .version
	$(CC) $(CFLAGS) -c -o $@ $<

$(ELF): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	$(STRIP) $@

clean:
	rm -f $(ELF) $(OBJS) $(HEADERS) .version
