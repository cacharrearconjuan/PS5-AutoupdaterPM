ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK is not set)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS += -I$(PS5_PAYLOAD_SDK)/target/include
LDFLAGS += -L$(PS5_PAYLOAD_SDK)/target/lib

ELF = hello_socket.elf
OBJS = main.o
# Incluimos mbedtls junto a curl
LDLIBS = -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lSceNet -lSceSysmodule

all: $(ELF)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(ELF): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(ELF) $(OBJS)