# ============================================================================
# IMS Project Makefile
#
# RTOS: Zephyr (target: rpi_4b)
#
# The server binary is now built by Zephyr's west tool, NOT by this Makefile.
# This Makefile only builds the Linux-side tools:
#   - ims_client   (terminal client that connects to the Zephyr server)
#
# ============================================================================
# Zephyr server build commands:
#   west build -b rpi_4b .          # build
#   west build -b rpi_4b . -t menuconfig  # configure
#
# SD card deployment (after build):
#   1. Copy build/zephyr/zephyr.bin to FAT32 SD card root
#   2. Ensure config.txt contains:
#        kernel=zephyr.bin
#        arm_64bit=1
#        enable_uart=1
#        uart_2ndstage=1
# ============================================================================

CC_LINUX = gcc

INCLUDES     = -I./apps -I./common -I./drivers -I./protocol
CFLAGS_LINUX = -D_GNU_SOURCE -Wall -Wextra $(INCLUDES)
LIBS_LINUX   = -lssl -lcrypto -lpthread

SRC_CLIENT = apps/client.c

TARGET_CLIENT = ims_client

# ============================================================================
# Build Targets
# ============================================================================

all: client_linux

# Linux terminal client (connects to Zephyr server over mTLS)
client_linux:
	@echo "[INFO] Building Linux Client..."
	$(CC_LINUX) $(CFLAGS_LINUX) -o $(TARGET_CLIENT) $(SRC_CLIENT) $(LIBS_LINUX)
	@echo "[OK] $(TARGET_CLIENT) built."

clean:
	@echo "[INFO] Cleaning up..."
	rm -f $(TARGET_CLIENT) *.o
	@echo "[INFO] To clean Zephyr build artefacts run:  rm -rf build/"

# Generate certs then convert to C headers for Zephyr embedding
certs:
	@./scripts/quick_start.sh
	@./scripts/gen_cert_headers.sh

.PHONY: all client_linux clean certs
