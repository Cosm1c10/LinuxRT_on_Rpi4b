#!/bin/bash
# ============================================================================
# IMS Quick Start Script  —  Zephyr / Raspberry Pi 4 Edition
# ============================================================================
#
# Usage:
#   ./scripts/quick_start.sh              # generate certs + build client
#   ./scripts/quick_start.sh all          # also generate cert headers + west build
#   ./scripts/quick_start.sh certs        # cert generation only
#   ./scripts/quick_start.sh headers      # PEM -> C header conversion only
#
# Prerequisites (host machine):
#   - openssl
#   - gcc + libssl-dev          (for the Linux terminal client)
#   - Zephyr SDK + west         (for the RPi 4 server image)
#
# ============================================================================

set -e
cd "$(dirname "$0")/.."

# Configuration
OPENSSL_CMD="${OPENSSL_CMD:-openssl}"
export OPENSSL_CONF="${OPENSSL_CONF:-/etc/ssl/openssl.cnf}"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

log()   { echo -e "${GREEN}[INFO]  $1${NC}"; }
warn()  { echo -e "${YELLOW}[WARN]  $1${NC}"; }
error() { echo -e "${RED}[ERROR] $1${NC}"; exit 1; }

# ============================================================================
# 1. Generate TLS Certificates
# ============================================================================
generate_certs() {
    log "Generating TLS certificates in certs/ ..."
    mkdir -p certs

    if [ ! -f "$OPENSSL_CMD" ] && ! command -v openssl &>/dev/null; then
        error "openssl not found. Please install it."
    fi
    [ -f "$OPENSSL_CMD" ] || OPENSSL_CMD="openssl"

    # CA
    if [ ! -f "certs/ca.key" ]; then
        $OPENSSL_CMD genrsa -out certs/ca.key 2048 2>/dev/null
        $OPENSSL_CMD req -x509 -new -nodes -key certs/ca.key \
            -sha256 -days 365 -out certs/ca.crt \
            -subj "/CN=IMS_Root_CA"
        log "CA certificate created."
    else
        log "CA certificate already exists, skipping."
    fi

    # Server certificate
    if [ ! -f "certs/server.key" ]; then
        $OPENSSL_CMD genrsa -out certs/server.key 2048 2>/dev/null
        $OPENSSL_CMD req -new -key certs/server.key \
            -out certs/server.csr \
            -subj "/CN=ims_server/O=IMS"
        $OPENSSL_CMD x509 -req -in certs/server.csr \
            -CA certs/ca.crt -CAkey certs/ca.key -CAcreateserial \
            -out certs/server.crt -days 365 -sha256
        log "Server certificate created."
    else
        log "Server certificate already exists, skipping."
    fi

    # Client certificates (one per role)
    ROLES=("admin:ADMIN" "operator:OPERATOR" "viewer:VIEWER" "maintenance:MAINTENANCE")
    for role_info in "${ROLES[@]}"; do
        role_name="${role_info%%:*}"
        role_ou="${role_info##*:}"
        CLIENT_NAME="${role_name}_client"
        if [ ! -f "certs/${CLIENT_NAME}.key" ]; then
            log "Creating certificate for: $CLIENT_NAME (OU=$role_ou)"
            $OPENSSL_CMD genrsa -out "certs/${CLIENT_NAME}.key" 2048 2>/dev/null
            $OPENSSL_CMD req -new \
                -key "certs/${CLIENT_NAME}.key" \
                -out "certs/${CLIENT_NAME}.csr" \
                -subj "/CN=${CLIENT_NAME}/O=IMS/OU=${role_ou}"
            $OPENSSL_CMD x509 -req \
                -in "certs/${CLIENT_NAME}.csr" \
                -CA certs/ca.crt -CAkey certs/ca.key -CAcreateserial \
                -out "certs/${CLIENT_NAME}.crt" -days 365 -sha256
        fi
    done

    # Clean intermediate files
    rm -f certs/*.csr certs/*.srl 2>/dev/null

    # Default client cert = admin (for quick testing)
    cp certs/admin_client.crt certs/client.crt
    cp certs/admin_client.key certs/client.key

    log "Certificates ready in certs/"
}

# ============================================================================
# 2. Convert PEM certs to C arrays for Zephyr embedding
# ============================================================================
generate_cert_headers() {
    log "Converting PEM certs to C arrays (drivers/certs/certs.h) ..."
    if [ ! -f "certs/ca.crt" ]; then
        error "certs/ca.crt not found. Run certificate generation first."
    fi
    ./scripts/gen_cert_headers.sh
    log "drivers/certs/certs.h updated."
}

# ============================================================================
# 3. Build Linux terminal client
# ============================================================================
build_client() {
    log "Building Linux terminal client (ims_client) ..."
    make client_linux
    log "ims_client built successfully."
}

# ============================================================================
# 4. Build Zephyr server image (requires west + Zephyr SDK)
# ============================================================================
build_zephyr() {
    log "Building Zephyr server image for rpi_4b ..."
    if ! command -v west &>/dev/null; then
        warn "west not found — skipping Zephyr build."
        warn "Install Zephyr SDK and run:  west build -b rpi_4b ."
        return
    fi
    west build -b rpi_4b .
    log "Zephyr build complete.  Output: build/zephyr/zephyr.bin"
}

# ============================================================================
# 5. SD card deployment instructions
# ============================================================================
show_deploy_instructions() {
    echo ""
    echo -e "${YELLOW}=== SD Card Deployment ===${NC}"
    echo "1. Format a micro-SD card: MBR partition table, single FAT32 partition."
    echo "2. Download firmware files to the SD card root:"
    echo "     https://raw.githubusercontent.com/raspberrypi/firmware/master/boot/bcm2711-rpi-4-b.dtb"
    echo "     https://raw.githubusercontent.com/raspberrypi/firmware/master/boot/start4.elf"
    echo "     https://raw.githubusercontent.com/raspberrypi/firmware/master/boot/fixup4.dat"
    echo "3. Copy the Zephyr image:"
    echo "     cp build/zephyr/zephyr.bin <SD_CARD_MOUNT>/"
    echo "4. Create <SD_CARD_MOUNT>/config.txt with:"
    echo "     kernel=zephyr.bin"
    echo "     arm_64bit=1"
    echo "     enable_uart=1"
    echo "     uart_2ndstage=1"
    echo "5. Insert card and power on.  Monitor UART (GPIO14/15, 115200 baud)."
    echo ""
    echo -e "${GREEN}=== Linux Client Usage ===${NC}"
    echo "  ./ims_client <RPi_IP_ADDRESS>"
    echo ""
}

# ============================================================================
# Entry point
# ============================================================================
case "${1:-}" in
    certs)
        generate_certs
        ;;
    headers)
        generate_cert_headers
        ;;
    all)
        generate_certs
        generate_cert_headers
        build_client
        build_zephyr
        show_deploy_instructions
        ;;
    "")
        generate_certs
        generate_cert_headers
        build_client
        show_deploy_instructions
        ;;
    *)
        error "Unknown option '$1'. Use: certs | headers | all | (empty)"
        ;;
esac

log "Done."
