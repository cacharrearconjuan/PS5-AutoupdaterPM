PS5_PAYLOAD_SDK ?= /opt/pacbrew/ps5/payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := PS5-AutoupdaterPM.elf
SRCS := main.c

CURL_CFLAGS := $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
CURL_LIBS := $(shell $(PKG_CONFIG) --libs --static libcurl 2>/dev/null || echo "-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz")

CFLAGS := -Wall -O2 $(CURL_CFLAGS)
LDADD := $(CURL_LIBS)

all: $(ELF)

$(ELF): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDADD)

clean:
	rm -f $(ELF) *.o
