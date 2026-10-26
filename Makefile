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

# Dónde guarda Payload Manager el actualizador; el icono lanza esta copia
UPDATER_PATH := /data/pldmgr/PS5-AutoupdaterPM.elf

# App del icono: eboot.bin (firmado en falso) + param.json + icon0.png.
# El enlazado y la firma son los del ejemplo samples/install_app del SDK.
APP_SAMPLE := $(PS5_PAYLOAD_SDK)/samples/install_app
ELF2SELF   := python3 $(APP_SAMPLE)/make_fself.py
EBOOT_LD   := $(APP_SAMPLE)/eboot.x

PS4_SDK_VERSION ?= 0x07590001
AUTHID   := 0x3800000000000022
AUTHINFO := 00 00 00 00 00 00 00 00 00 00 00 00 00 1C 00 40 00 FF 00 00 00 00 \
            00 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 \
            00 40 00 40 00 00 00 00 00 00 00 80 00 00 00 00 00 00 00 08 00 40 \
            FF FF 00 00 00 F0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
            00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
            00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 \
            00 00 00 00

HEADERS := cacert_pem.h eboot_bin.h app_param_json.h app_param_system_json.h icon0_png.h

CFLAGS  := -Os -Wall -ffunction-sections -fdata-sections \
           -I$(PS5_PAYLOAD_SDK)/target/include \
           -DAPP_VERSION=\"$(VERSION)\" -DUPDATER_PATH=\"$(UPDATER_PATH)\"
LDFLAGS := -L$(PS5_PAYLOAD_SDK)/target/lib -Wl,--gc-sections
LDLIBS  := -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lpthread -lSceAppInstUtil

EBOOT_CFLAGS := -Os -Wall -Werror -ffreestanding -fno-builtin -I.

.PHONY: all clean FORCE

all: $(ELF)

# Recompila main.o cuando cambia VERSION
.version: FORCE
	@echo '$(VERSION)' | cmp -s - $@ || echo '$(VERSION)' > $@

$(CA_PEM):
	curl -fsSL -o $@ $(CA_URL)

# --- App del icono ---------------------------------------------------------

# 1. Lanzador: payload normal que manda UPDATER_PATH a elfldr
lanzador.elf: app/lanzador.c app/pm_slots.h
	$(CC) -Os -Wall -Werror -DUPDATER_PATH=\"$(UPDATER_PATH)\" -o $@ $<
	$(STRIP) $@

lanzador_elf.h: lanzador.elf tools/bin2h.py
	python3 tools/bin2h.py $< $@ lanzador_elf

# 2. eboot: ejecutable autónomo que lleva dentro el lanzador
eboot.o: app/eboot.c lanzador_elf.h
	$(CC) -c $(EBOOT_CFLAGS) -o $@ $<

eboot.elf: eboot.o $(EBOOT_LD)
	$(LD) --static -T $(EBOOT_LD) -o $@ eboot.o

eboot.bin: eboot.elf
	$(ELF2SELF) --ptype fake --paid $(AUTHID) \
	            --auth-info "$(AUTHINFO)" \
	            --app-version $(PS4_SDK_VERSION) \
	            --fw-version $(PS4_SDK_VERSION) $< $@

# 3. Todo se incrusta en el actualizador
eboot_bin.h: eboot.bin tools/bin2h.py
	python3 tools/bin2h.py $< $@ eboot_bin

app_param_json.h: app/sce_sys/param.json tools/bin2h.py
	python3 tools/bin2h.py $< $@ app_param_json

app_param_system_json.h: app/sce_sys/param.json.system tools/bin2h.py
	python3 tools/bin2h.py $< $@ app_param_system_json

icon0_png.h: app/sce_sys/icon0.png tools/bin2h.py
	python3 tools/bin2h.py $< $@ icon0_png

# --- Actualizador ----------------------------------------------------------

cacert_pem.h: $(CA_PEM) tools/bin2h.py
	python3 tools/bin2h.py $< $@ cacert_pem

main.o: main.c app/pm_slots.h $(HEADERS) .version
	$(CC) $(CFLAGS) -c -o $@ $<

$(ELF): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	$(STRIP) $@

clean:
	rm -f $(ELF) $(OBJS) $(HEADERS) lanzador_elf.h lanzador.elf eboot.o eboot.elf eboot.bin .version
