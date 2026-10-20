PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := PS5-AutoupdaterPM.elf
SRCS := main.c

CFLAGS := -Wall -O2 -I$(PS5_PAYLOAD_SDK)/target/user/include
LDADD := -L$(PS5_PAYLOAD_SDK)/target/user/lib -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz

all: $(ELF)

$(ELF): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDADD)

clean:
	rm -f $(ELF) *.o
