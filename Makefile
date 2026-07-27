PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
CC = $(PS5_PAYLOAD_SDK)/bin/x86_64-sony-prospero-gcc
OBJCOPY = $(PS5_PAYLOAD_SDK)/bin/x86_64-sony-prospero-objcopy

CFLAGS = -I$(PS5_PAYLOAD_SDK)/include -O2 -Wall
LDFLAGS = -L$(PS5_PAYLOAD_SDK)/lib -lcurl -lssl -lcrypto -lSceSysmodule -lSceSysUtil -lSceAppMgr

TARGET = autoupdater_pldmgr.elf

all: $(TARGET)

$(TARGET): main.o
	$(CC) $^ -o $@ $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET)