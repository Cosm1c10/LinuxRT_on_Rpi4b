# Sentinel-RT — Industrial Equipment Health Dashboard

**Sentinel-RT** is a real-time industrial equipment monitoring system that runs on a **Raspberry Pi 4** using the **Zephyr RTOS**. It continuously reads vibration, sound, temperature, and current sensors and serves the data over a **Mutual TLS (mTLS) encrypted TCP connection** to remote clients. Access is controlled by a **Role-Based Access Control (RBAC)** system embedded in X.509 certificates.

> **RTOS:** Zephyr (migrated from QNX Neutrino 8.0)
> **Board Target:** `rpi_4b`
> **Security:** mTLS with MbedTLS
> **Author:** Hemanth Kumar — [@Hemanthkumar04](https://github.com/Hemanthkumar04)

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Repository Structure](#2-repository-structure)
3. [Hardware Setup](#3-hardware-setup)
4. [Host Machine Prerequisites](#4-host-machine-prerequisites)
5. [Install the Zephyr SDK](#5-install-the-zephyr-sdk)
6. [Set Up a West Workspace](#6-set-up-a-west-workspace)
7. [Generate TLS Certificates](#7-generate-tls-certificates)
8. [Embed Certificates into the Firmware](#8-embed-certificates-into-the-firmware)
9. [Build the Zephyr Server Image](#9-build-the-zephyr-server-image)
10. [Prepare the SD Card](#10-prepare-the-sd-card)
11. [Flash / Deploy Zephyr to the Raspberry Pi 4](#11-flash--deploy-zephyr-to-the-raspberry-pi-4)
12. [Monitor the Boot Console (UART)](#12-monitor-the-boot-console-uart)
13. [Build the Linux Terminal Client](#13-build-the-linux-terminal-client)
14. [Run the Python Dashboard Client](#14-run-the-python-dashboard-client)
15. [Command Reference](#15-command-reference)
16. [Security Architecture](#16-security-architecture)
17. [Sensor Health Thresholds](#17-sensor-health-thresholds)
18. [Sensor Specifications](#18-sensor-specifications)
19. [Troubleshooting](#19-troubleshooting)
20. [Project Architecture Deep-Dive](#20-project-architecture-deep-dive)
21. [Future Roadmap](#21-future-roadmap)

---

## 1. System Overview

### What the system does

```
┌──────────────────────────────┐         ┌───────────────────────────────┐
│   RASPBERRY PI 4  (Zephyr)   │         │   CLIENT MACHINE  (Linux/Mac) │
│                              │  mTLS   │                               │
│  GPIO 17 ← SW-420 Vibration  │◄───────►│  ./ims_client <RPi_IP>        │
│  GPIO 27 ← LM393 Sound       │  TCP    │  python3 clients/dashboard.py │
│  GPIO  4 ← DS18B20 Temp(1W)  │  8080   │                               │
│  I2C1    ← ADS1115 (Current) │         └───────────────────────────────┘
│                              │
│  [1 kHz polling thread]      │
│  [Zephyr TLS server :8080]   │
└──────────────────────────────┘
```

The RPi 4 runs **Zephyr RTOS** as a bare-metal image (no Linux underneath). Zephyr boots directly from an SD card, initialises GPIO and I2C, starts a background 1 kHz sensor-polling thread, and then listens for TLS client connections on port 8080.

### Key features

| Feature | Detail |
|---------|--------|
| Real-time sampling | 1 kHz digital GPIO polling; 1 Hz I2C and 1-Wire reads |
| Security | Mutual TLS 1.2, MbedTLS, X.509 role certificates |
| Access control | 4 roles: ADMIN, OPERATOR, MAINTENANCE, VIEWER |
| Live telemetry | Push-based `monitor` command, 1 s update interval |
| Black-box log | In-memory 4 KB ring buffer of CRITICAL events |
| Multi-client | Up to 32 concurrent authenticated sessions |
| Clients | C terminal client + Python Matplotlib dashboard |

---

## 2. Repository Structure

```
Edge-Based-Industrial-Equipment-Health-Dashboard/
│
├── CMakeLists.txt              ← Zephyr build entry point
├── prj.conf                    ← Zephyr Kconfig (TLS, networking, GPIO, I2C)
├── Makefile                    ← Builds the Linux-side terminal client only
│
├── boards/
│   └── rpi_4b.overlay          ← Device tree: enables I2C1, documents GPIO pins
│
├── apps/
│   ├── server.c                ← Zephyr TLS server (main entry point)
│   └── client.c                ← Linux terminal client (OpenSSL mTLS)
│
├── clients/
│   └── dashboard.py            ← Python real-time graphical dashboard
│
├── common/
│   ├── authorization.h         ← Role definitions, authorize_client() prototype
│   └── authorization.c         ← MbedTLS X.509 CN/OU extraction (Zephyr)
│                                  OpenSSL X.509 extraction (Linux fallback)
│
├── drivers/
│   ├── sensors.h               ← GPIO pin definitions, HAL prototypes
│   ├── sensors.c               ← Zephyr GPIO/I2C/1-Wire HAL
│   ├── sensor_manager.h        ← SensorManager struct, public API
│   ├── sensor_manager.c        ← 1 kHz polling thread, health evaluation
│   └── certs/
│       └── certs.h             ← Auto-generated C arrays of server certs
│
├── protocol/
│   ├── protocol.h              ← ProtocolContext, command prototypes
│   └── protocol.c              ← Command handlers, monitor loop, log ring buffer
│
├── scripts/
│   ├── quick_start.sh          ← One-command: cert gen + client build + Zephyr build
│   └── gen_cert_headers.sh     ← Converts PEM certs → C arrays in drivers/certs/certs.h
│
├── tests/
│   └── sensor_test.c           ← Standalone hardware diagnostic tool
│
├── config/
│   └── client_roles.conf       ← Reference: CN → role mappings
│
└── certs/                      ← Generated at runtime (gitignored)
    ├── ca.crt / ca.key
    ├── server.crt / server.key
    ├── admin_client.crt/key
    ├── operator_client.crt/key
    ├── viewer_client.crt/key
    ├── maintenance_client.crt/key
    └── client.crt/key          ← Symlink to admin_client (default)
```

---

## 3. Hardware Setup

### Bill of Materials

| # | Component | Model | Purpose |
|---|-----------|-------|---------|
| 1 | SBC | Raspberry Pi 4 Model B (any RAM) | Host for Zephyr RTOS |
| 2 | Vibration sensor | SW-420 | Detects mechanical shock/vibration |
| 3 | Sound sensor | LM393 microphone module | Measures acoustic duty cycle |
| 4 | Temperature sensor | DS18B20 (TO-92 or waterproof) | 1-Wire digital temperature |
| 5 | Current sensor | ACS712 (±20 A variant) | Hall-effect current sensing |
| 6 | ADC | ADS1115 (16-bit I2C) | Converts ACS712 analogue output |
| 7 | Resistor | 4.7 kΩ | DS18B20 1-Wire pull-up |
| 8 | LED (optional) | Any 3.3 V LED + 330 Ω resistor | Status indicator |
| 9 | Micro-SD card | ≥ 4 GB, Class 10 | Boots Zephyr image |
| 10 | USB-UART adapter | CP2102 / CH340 / FTDI | Monitor serial console |

### RPi 4 GPIO Pinout Reference

```
                    3V3  (1) (2)  5V
   SDA1 / GPIO  2   (3) (4)  5V
   SCL1 / GPIO  3   (5) (6)  GND
  1-Wire / GPIO  4  (7) (8)  GPIO 14  ← UART TX (console out)
              GND  (9) (10)  GPIO 15  ← UART RX (console in)
    Vib / GPIO 17  (11) (12)  GPIO 18
    Snd / GPIO 27  (13) (14)  GND
    LED / GPIO 22  (15) (16)  GPIO 23
              3V3 (17) (18)  GPIO 24
             ...
```

### Wiring Diagram

#### SW-420 Vibration Sensor

| SW-420 Pin | RPi Physical Pin | Note |
|-----------|-----------------|------|
| VCC | Pin 1 (3.3 V) | |
| GND | Pin 6 (GND) | |
| DO | Pin 11 (GPIO 17) | Digital output |

#### LM393 Sound Sensor Module

| LM393 Pin | RPi Physical Pin | Note |
|-----------|-----------------|------|
| VCC | Pin 1 (3.3 V) | |
| GND | Pin 6 (GND) | |
| DO | Pin 13 (GPIO 27) | Digital output — adjust onboard pot until LED triggers only on loud sounds |

#### DS18B20 Temperature Sensor

| DS18B20 Pin | RPi Physical Pin | Note |
|-------------|-----------------|------|
| VDD | Pin 1 (3.3 V) | Normal power mode |
| GND | Pin 6 (GND) | |
| DATA | Pin 7 (GPIO 4) | **MUST add 4.7 kΩ pull-up between VDD and DATA** |

```
  3.3V ──┬──[4.7kΩ]──┬── GPIO 4
         │           │
        VDD         DATA
        GND ─────── GND
```

#### ADS1115 ADC (I2C)

| ADS1115 Pin | RPi Physical Pin | Note |
|-------------|-----------------|------|
| VDD | Pin 1 (3.3 V) | |
| GND | Pin 6 (GND) | |
| SDA | Pin 3 (GPIO 2) | I2C1 data |
| SCL | Pin 5 (GPIO 3) | I2C1 clock |
| ADDR | GND | Sets I2C address to 0x48 |
| A0 | ACS712 OUT | Analogue input channel 0 |

#### ACS712 Current Sensor

| ACS712 Pin | Connection | Note |
|-----------|-----------|------|
| VCC | RPi Pin 2 (5 V) | Requires 5 V supply |
| GND | RPi Pin 6 (GND) | |
| OUT | ADS1115 A0 | 0–5 V analogue (2.5 V = 0 A) |
| IP+ / IP- | In series with monitored load | Break the circuit being measured |

#### USB-UART Adapter (Serial Console)

| UART Adapter | RPi Physical Pin | Note |
|-------------|-----------------|------|
| GND | Pin 6 (GND) | Common ground |
| RX | Pin 8 (GPIO 14) | RPi TX → Adapter RX |
| TX | Pin 10 (GPIO 15) | RPi RX ← Adapter TX |
| 3.3 V | **Do NOT connect** | RPi is self-powered |

> **Baud rate:** 115200 8N1

---

## 4. Host Machine Prerequisites

Everything below runs on a **Linux workstation** (Ubuntu 22.04 / Debian 12 recommended). WSL2 on Windows also works. macOS works with Homebrew equivalents.

```bash
# Core build tools
sudo apt update
sudo apt install -y \
    git cmake ninja-build gperf \
    ccache dfu-util device-tree-compiler wget \
    python3-dev python3-pip python3-setuptools python3-tk python3-wheel \
    xz-utils file make gcc gcc-multilib g++-multilib \
    libsdl2-dev libmagic1 \
    openssl libssl-dev \
    minicom picocom        # for UART console monitoring

# Python packages
pip3 install --user west matplotlib numpy
```

Verify that `west` is on your PATH:

```bash
west --version
# Expected: west, version 1.x.x
```

---

## 5. Install the Zephyr SDK

The Zephyr SDK provides the `aarch64-zephyr-elf` cross-compiler needed to build for the RPi 4.

### Step 1 — Download the SDK bundle

Go to the [Zephyr SDK releases page](https://github.com/zephyrproject-rtos/sdk-ng/releases) and download the latest minimal bundle for your host architecture. For a 64-bit Linux host:

```bash
cd ~
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/sha256.sum
sha256sum --check --ignore-missing sha256.sum
```

> Replace `v0.16.8` with whatever is the latest stable release.

### Step 2 — Extract and install

```bash
tar -xvf zephyr-sdk-0.16.8_linux-x86_64_minimal.tar.xz
cd zephyr-sdk-0.16.8

# Install the SDK (sets up udev rules and CMake packages)
./setup.sh -t aarch64-zephyr-elf -h -c
```

The `-t aarch64-zephyr-elf` flag installs only the ARM 64-bit toolchain (needed for RPi 4). This is significantly faster than installing all toolchains.

### Step 3 — Set environment variable

```bash
# Add to ~/.bashrc (or ~/.zshrc) so it persists across sessions
echo 'export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-0.16.8' >> ~/.bashrc
source ~/.bashrc
```

### Step 4 — Verify the toolchain

```bash
aarch64-zephyr-elf-gcc --version
# Expected: aarch64-zephyr-elf-gcc (Zephyr SDK ...) 12.x.x
```

---

## 6. Set Up a West Workspace

Zephyr uses **west** as its meta-tool for managing the repository and its many dependencies. You initialise a workspace once per machine.

### Step 1 — Create a workspace directory

```bash
mkdir ~/zephyrproject
cd ~/zephyrproject
```

### Step 2 — Initialise the workspace

```bash
west init .
# This downloads the Zephyr manifest and creates the .west/ folder
```

### Step 3 — Pull all dependencies

```bash
west update
# Downloads Zephyr kernel, HAL for BCM2711, MbedTLS, etc.
# This can take 5–15 minutes on first run.
```

### Step 4 — Set up Python requirements

```bash
pip3 install --user -r ~/zephyrproject/zephyr/scripts/requirements.txt
```

### Step 5 — Export CMake packages

```bash
west zephyr-export
```

### Step 6 — Verify the environment

```bash
cd ~/zephyrproject/zephyr
west build -b rpi_4b samples/hello_world
# Should complete without errors and produce build/zephyr/zephyr.bin
```

If the hello world builds successfully, your environment is ready.

### Step 7 — Link this project into the workspace

Place the cloned project **inside** the west workspace so west can find it:

```bash
# Option A: Clone directly into the workspace
cd ~/zephyrproject
git clone https://github.com/Hemanthkumar04/Edge-Based-Industrial-Equipment-Health-Dashboard.git sentinel_rt
cd sentinel_rt

# Option B: Symlink an existing clone
ln -s /path/to/your/clone ~/zephyrproject/sentinel_rt
cd ~/zephyrproject/sentinel_rt
```

> `ZEPHYR_BASE` must point to the Zephyr kernel source. West sets this automatically when you run `west build` from inside a workspace. If you build outside the workspace, set it manually:
> ```bash
> export ZEPHYR_BASE=~/zephyrproject/zephyr
> ```

---

## 7. Generate TLS Certificates

All TLS certificates are generated on your host machine using OpenSSL. This creates:

- A private Certificate Authority (CA)
- A server certificate (loaded into the Zephyr firmware as C arrays)
- Four client certificates (one per role: admin, operator, viewer, maintenance)

Run the generation script:

```bash
cd ~/zephyrproject/sentinel_rt
./scripts/quick_start.sh certs
```

What this does internally:

```
certs/
├── ca.key          ← CA private key  (KEEP SECRET)
├── ca.crt          ← CA certificate  (shared with all clients)
├── server.key      ← Server private key  (embedded in firmware)
├── server.crt      ← Server certificate
├── admin_client.key/crt      ← ADMIN role
├── operator_client.key/crt   ← OPERATOR role
├── viewer_client.key/crt     ← VIEWER role
├── maintenance_client.key/crt ← MAINTENANCE role
├── client.key      ← Default client key (copy of admin_client.key)
└── client.crt      ← Default client cert (copy of admin_client.crt)
```

> **Security note:** The `certs/` directory is in `.gitignore`. Never commit private keys to git. Each team member should generate their own certificates or share them out-of-band.

### Distributing client certificates to teammates

```bash
# For each teammate, give them their role-appropriate cert + the CA cert:
scp certs/operator_client.crt certs/operator_client.key certs/ca.crt teammate@machine:~/sentinel_rt/certs/

# They rename to the default paths:
cp certs/operator_client.crt certs/client.crt
cp certs/operator_client.key certs/client.key
```

---

## 8. Embed Certificates into the Firmware

Because Zephyr runs bare-metal (no filesystem), TLS certificates cannot be loaded from files at runtime. Instead they are compiled into the firmware as C byte arrays.

Run the header generator:

```bash
./scripts/gen_cert_headers.sh
```

This reads `certs/ca.crt`, `certs/server.crt`, `certs/server.key`, converts them to DER format, and writes them into `drivers/certs/certs.h` as `static const unsigned char[]` arrays.

You can verify the output:

```bash
head -30 drivers/certs/certs.h
# Should show: static const unsigned char ca_cert_der[] = { 0x30, 0x82, ...
```

> **Important:** You must re-run `gen_cert_headers.sh` every time you regenerate certificates, then rebuild the Zephyr image.

---

## 9. Build the Zephyr Server Image

From the project root (inside your west workspace):

```bash
west build -b rpi_4b .
```

### What this does

1. CMake reads `CMakeLists.txt` and `prj.conf`
2. Zephyr's build system compiles the kernel + all enabled drivers (networking, TLS, GPIO, I2C)
3. Your application sources are compiled and linked against the Zephyr kernel
4. The output is a single binary: `build/zephyr/zephyr.bin`

### Expected output

```
-- west build: generating a build system
...
[  1/342] Preparing syscall dependency handling
...
[342/342] Linking C executable zephyr/zephyr.elf
Memory region         Used Size  Region Size  %age Used
           FLASH:      412672 B         1 MB     39.36%
            SRAM:       65536 B       128 MB      0.05%
        IDT_LIST:          0 GB        32 KB      0.00%
```

### Useful build variants

```bash
# Clean rebuild
west build -b rpi_4b . --pristine

# Open interactive configuration menu
west build -b rpi_4b . -t menuconfig

# Verbose build output (useful for debugging compile errors)
west build -b rpi_4b . -- -DCMAKE_VERBOSE_MAKEFILE=ON
```

---

## 10. Prepare the SD Card

Zephyr boots from a FAT32 micro-SD card on the RPi 4. The boot process is:

```
Power ON → GPU reads SD card → loads start4.elf → reads config.txt
        → loads zephyr.bin → Zephyr kernel starts
```

### Step 1 — Format the SD card

The SD card needs:
- **Partition table:** MBR (not GPT)
- **Partition 1:** FAT32, at least 64 MB, bootable flag set

On Linux (replace `/dev/sdX` with your actual SD card device — verify with `lsblk`):

```bash
# WARNING: This ERASES the card. Double-check /dev/sdX is your SD card.
sudo fdisk /dev/sdX <<EOF
o       # Create new MBR partition table
n       # New partition
p       # Primary
1       # Partition number 1
        # Default first sector
+256M   # 256 MB FAT32 partition
t       # Change type
b       # FAT32
a       # Toggle bootable flag
w       # Write and exit
EOF

sudo mkfs.vfat -F 32 -n BOOT /dev/sdX1
```

Alternatively, use **GParted** (GUI) or **Raspberry Pi Imager** to format (choose "Erase" option to get a clean FAT32 card).

### Step 2 — Mount the SD card

```bash
sudo mkdir -p /mnt/sdcard
sudo mount /dev/sdX1 /mnt/sdcard
```

### Step 3 — Download RPi 4 firmware files

These files are the GPU bootloader blobs provided by the Raspberry Pi Foundation. Zephyr requires them.

```bash
cd /mnt/sdcard

# GPU firmware
wget https://raw.githubusercontent.com/raspberrypi/firmware/master/boot/start4.elf
wget https://raw.githubusercontent.com/raspberrypi/firmware/master/boot/fixup4.dat

# Device tree blob for BCM2711 (RPi 4)
wget https://raw.githubusercontent.com/raspberrypi/firmware/master/boot/bcm2711-rpi-4-b.dtb
```

### Step 4 — Copy the Zephyr binary

```bash
cp ~/zephyrproject/sentinel_rt/build/zephyr/zephyr.bin /mnt/sdcard/
```

### Step 5 — Create `config.txt`

Create the file `/mnt/sdcard/config.txt` with exactly this content:

```bash
cat > /mnt/sdcard/config.txt << 'EOF'
kernel=zephyr.bin
arm_64bit=1
enable_uart=1
uart_2ndstage=1
EOF
```

| Option | Effect |
|--------|--------|
| `kernel=zephyr.bin` | Tells the GPU bootloader to load and execute `zephyr.bin` |
| `arm_64bit=1` | Boot in 64-bit AArch64 mode (required for Zephyr on RPi 4) |
| `enable_uart=1` | Enables the mini UART / PL011 for serial console |
| `uart_2ndstage=1` | Keeps UART enabled during the GPU second-stage boot |

### Step 6 — Unmount

```bash
sync
sudo umount /mnt/sdcard
```

Your SD card is ready. The final card contents should look like:

```
/
├── bcm2711-rpi-4-b.dtb
├── start4.elf
├── fixup4.dat
├── zephyr.bin
└── config.txt
```

---

## 11. Flash / Deploy Zephyr to the Raspberry Pi 4

1. Insert the prepared micro-SD card into the RPi 4's SD card slot.
2. Connect your USB-UART adapter to GPIO 14 (TX) and GPIO 15 (RX) as described in [Section 3](#3-hardware-setup).
3. Connect your USB-UART adapter to your host machine.
4. **Do not power on yet** — open the serial monitor first (next section).
5. Connect the RPi 4 power supply (5 V / 3 A via USB-C).

That's the entire "flashing" process for Zephyr on RPi 4. There is no `west flash` command — you physically swap the SD card. For iterative development, keep the SD card mounted and overwrite `zephyr.bin`:

```bash
# Quick redeploy after rebuild:
sudo mount /dev/sdX1 /mnt/sdcard
cp build/zephyr/zephyr.bin /mnt/sdcard/
sync && sudo umount /mnt/sdcard
```

---

## 12. Monitor the Boot Console (UART)

The Zephyr kernel prints all `printf` / `printk` output to UART0 (GPIO 14/15). Connect your USB-UART adapter and use one of the following tools.

### Using minicom

```bash
sudo minicom -D /dev/ttyUSB0 -b 115200
# Exit with: Ctrl+A then X
```

### Using picocom

```bash
picocom -b 115200 /dev/ttyUSB0
# Exit with: Ctrl+A then Ctrl+X
```

### Using screen

```bash
screen /dev/ttyUSB0 115200
# Exit with: Ctrl+A then K then Y
```

> If your USB-UART adapter shows up as `/dev/ttyACM0` instead of `/dev/ttyUSB0`, adjust accordingly. Run `ls /dev/tty*` before and after plugging in to identify the correct device.

### Expected boot output

```
*** Booting Zephyr OS build v3.x.x-xxx ***
====================================================
   Sentinel-RT Monitoring System (Zephyr / RPi 4)
====================================================
[HW] GPIO and I2C initialized (Vib:GPIO17, Snd:GPIO27, 1W:GPIO4)
[SENSORS] Background polling thread started.
[TLS] Credentials loaded (CA=1, CERT=2, KEY=3)
[NETWORK] Listening on port 8080 (mTLS)

[NETWORK] Waiting for connection...
```

If you see the `[NETWORK] Listening on port 8080` line, the server is fully up. If anything before it fails, see [Section 19 — Troubleshooting](#19-troubleshooting).

### Finding the RPi 4's IP address

Zephyr obtains an IP via DHCP. Two ways to find it:

**From the UART console:** Add a short delay after DHCP and print the IP (requires customisation — see [Troubleshooting](#finding-the-rpi-4-ip-address)).

**From your router:** Check your router's DHCP client list. The RPi 4 will appear as a device with hostname `zephyr` or the MAC address of the RPi 4.

**Using nmap on your network:**
```bash
nmap -sn 192.168.1.0/24 | grep -A2 "Raspberry\|zephyr"
```

---

## 13. Build the Linux Terminal Client

The terminal client (`ims_client`) runs on your Linux workstation and connects to the Zephyr server over mTLS.

```bash
cd ~/zephyrproject/sentinel_rt
make client_linux
```

Expected output:
```
[INFO] Building Linux Client...
gcc -D_GNU_SOURCE -Wall -Wextra ... -o ims_client apps/client.c -lssl -lcrypto -lpthread
[OK] ims_client built.
```

### Connect to the server

```bash
./ims_client <RPi4_IP_ADDRESS>
```

Example:
```
[INFO] Connecting securely to 192.168.1.42:8080...

✓ Connected securely to server.
Type 'help' for available commands:

--- Connected to Sentinel-RT Secure Server ---
IMS>
```

The client uses the certificates in `certs/client.crt` and `certs/client.key` (which default to the admin role). To connect with a different role:

```bash
# Temporarily use the viewer certificate
cp certs/viewer_client.crt certs/client.crt
cp certs/viewer_client.key certs/client.key
./ims_client 192.168.1.42
```

---

## 14. Run the Python Dashboard Client

The Python dashboard (`clients/dashboard.py`) provides a real-time graphical view of all sensor data.

### Install dependencies

```bash
pip3 install matplotlib numpy
# Or via apt:
sudo apt install python3-matplotlib python3-numpy
```

### Configure and run

Open `clients/dashboard.py` and verify the server IP:

```python
SERVER_IP   = '192.168.1.42'    # ← Change to your RPi 4's IP
SERVER_PORT = 8080
```

Then run:

```bash
cd ~/zephyrproject/sentinel_rt
python3 clients/dashboard.py
```

A window opens with four real-time graphs:
- **Vibration** (red) — events per second
- **Sound** (blue) — duty cycle %
- **Temperature** (orange) — degrees Celsius
- **Current** (green) — Amperes

Horizontal dashed lines show WARNING and CRITICAL thresholds.

> **Note:** The dashboard is under active development. Some features may be incomplete.

---

## 15. Command Reference

Once connected (terminal client or dashboard), these commands are available:

| Command | Description | Min Role |
|---------|-------------|----------|
| `help` | List available commands (filtered by your role) | VIEWER |
| `whoami` | Show your certificate CN and assigned role | VIEWER |
| `list_units` | List registered equipment units | VIEWER |
| `get_sensors` | Raw sensor snapshot: Vib / Sound / Temp / Current | VIEWER |
| `get_health` | Health status evaluation: HEALTHY / WARNING / CRITICAL | VIEWER |
| `get_log` | Display the in-memory black-box event log | VIEWER |
| `monitor [duration]` | Live 1 s telemetry stream. Press ENTER to stop | OPERATOR |
| `clear_log` | Wipe the black-box log | ADMIN |
| `quit` / `exit` | Gracefully disconnect | VIEWER |

### `monitor` duration examples

```
monitor          ← infinite (stop with ENTER)
monitor 30       ← 30 seconds
monitor 5m       ← 5 minutes
monitor 1h       ← 1 hour
```

### Role permission matrix

| Command | VIEWER | OPERATOR | MAINTENANCE | ADMIN |
|---------|:------:|:--------:|:-----------:|:-----:|
| help, whoami, list_units, get_sensors, get_health, get_log, quit | ✅ | ✅ | ✅ | ✅ |
| monitor | ❌ | ✅ | ✅ | ✅ |
| clear_log | ❌ | ❌ | ❌ | ✅ |

---

## 16. Security Architecture

### How mTLS works in this system

```
CLIENT                              ZEPHYR SERVER
  │                                       │
  │──── TCP SYN ──────────────────────►  │
  │◄─── TCP SYN-ACK ──────────────────  │
  │                                       │
  │──── TLS ClientHello ───────────────► │
  │◄─── TLS ServerHello + server.crt ── │  ← Server proves identity
  │──── client.crt ────────────────────► │  ← Client proves identity
  │◄─── TLS Finished ─────────────────  │
  │                                       │
  │   [Both certs verified against CA]    │
  │                                       │
  │──── Encrypted commands ────────────► │
  │◄─── Encrypted responses ───────────  │
```

1. **The CA is self-generated** — only certificates signed by `certs/ca.crt` are trusted.
2. **The server certificate** is embedded in the Zephyr firmware (in `drivers/certs/certs.h`).
3. **Client certificates** are loaded from `certs/client.crt` at runtime on the Linux side.
4. **Roles** are embedded in the certificate's `OU` (Organizational Unit) field, e.g. `OU=ADMIN`.
5. **MbedTLS** on Zephyr verifies the peer cert chain and extracts CN + OU via `mbedtls_x509_name`.

### Certificate file roles

| Certificate file | Who uses it | OU field |
|-----------------|------------|---------|
| `certs/admin_client.crt` | Admins — full access | `ADMIN` |
| `certs/operator_client.crt` | Control room operators | `OPERATOR` |
| `certs/maintenance_client.crt` | Field technicians | `MAINTENANCE` |
| `certs/viewer_client.crt` | Read-only dashboards | `VIEWER` |
| `certs/server.crt` | Embedded in Zephyr firmware | — |
| `certs/ca.crt` | Distributed to all clients | — |

### Regenerating certificates

If certificates expire (365-day validity) or are compromised:

```bash
rm -rf certs/
./scripts/quick_start.sh certs    # regenerate
./scripts/gen_cert_headers.sh     # update firmware headers
west build -b rpi_4b . --pristine # rebuild firmware
# Then re-flash SD card
```

---

## 17. Sensor Health Thresholds

Health status is evaluated once per second in `drivers/sensor_manager.c`:

| Status | Vibration (events/s) | Current (A) | Action |
|--------|---------------------|-------------|--------|
| HEALTHY | 0 – 99 | 0 – 14.9 | None |
| WARNING | 100 – 199 | — | Console warning |
| CRITICAL | ≥ 200 | ≥ 15 | Written to black-box log ring buffer |

Temperature threshold for CRITICAL: **80 °C**

To adjust thresholds, edit the `#define` values at the top of [drivers/sensor_manager.c](drivers/sensor_manager.c):

```c
#define VIB_WARNING_THRESHOLD  100.0
#define VIB_CRITICAL_THRESHOLD 200.0
#define TMP_CRITICAL_THRESHOLD  80.0
#define CUR_CRITICAL_THRESHOLD  15.0
```

### Black-box log

The black-box log stores CRITICAL events as timestamped strings in a 4 KB in-memory ring buffer. It is accessible via the `get_log` command. Use `clear_log` (ADMIN only) to wipe it. The buffer is lost on power cycle — this is a known limitation of the bare-metal Zephyr deployment (no filesystem). A LittleFS integration is on the roadmap.

---

## 18. Sensor Specifications

### DS18B20 Temperature Sensor

| Property | Value |
|----------|-------|
| Protocol | 1-Wire (Dallas/Maxim) |
| Supply voltage | 3.0 V – 5.5 V |
| Range | −55 °C to +125 °C |
| Accuracy | ±0.5 °C (−10 °C to +85 °C) |
| Resolution | 9 – 12 bit (configurable) |
| Conversion time | 750 ms (12-bit) |
| Pull-up required | 4.7 kΩ on data line |

> The current 1-Wire implementation in `drivers/sensors.c` issues the correct reset pulse but returns a simulated temperature. Full 1-Wire bit-bang (ROM Search 0xF0, Skip ROM 0xCC, Convert T 0x44, Read Scratchpad 0xBE) is on the roadmap.

### ACS712 Current Sensor (20 A variant)

| Property | Value |
|----------|-------|
| Supply voltage | 5 V |
| Sensitivity | 100 mV/A (20 A model) |
| Zero-current output | 2.5 V (Vcc/2) |
| Bandwidth | DC – 80 kHz |
| Formula | `I = (V_adc × 1.5 − 2.5) / 0.100` |

For the 5 A variant: sensitivity = 185 mV/A → change `0.100` to `0.185`.
For the 30 A variant: sensitivity = 66 mV/A → change `0.100` to `0.066`.

### ADS1115 ADC

| Property | Value |
|----------|-------|
| Resolution | 16-bit |
| Sample rate | 8 – 860 SPS (configurable) |
| PGA range | ±0.256 V – ±6.144 V |
| Config used | PGA = ±4.096 V, AIN0-GND, single-shot, 128 SPS |
| I2C address | 0x48 (ADDR pin → GND) |

---

## 19. Troubleshooting

### Build Issues

#### `west: command not found`
```bash
pip3 install --user west
# Ensure ~/.local/bin is in your PATH:
echo 'export PATH=$HOME/.local/bin:$PATH' >> ~/.bashrc && source ~/.bashrc
```

#### `CMake Error: find_package(Zephyr ...) failed`
```bash
# Ensure ZEPHYR_BASE is set and west zephyr-export was run:
export ZEPHYR_BASE=~/zephyrproject/zephyr
west zephyr-export
```

#### `aarch64-zephyr-elf-gcc: not found`
The Zephyr SDK is not installed or the toolchain path is wrong.
```bash
~/zephyr-sdk-0.16.8/setup.sh -t aarch64-zephyr-elf
```

#### `fatal error: 'mbedtls/ssl.h' file not found`
MbedTLS module not pulled. Run `west update` inside the workspace.

#### `drivers/certs/certs.h: placeholder — regenerate`
The cert headers contain placeholder data. You must:
```bash
./scripts/quick_start.sh certs    # generate PEM certs
./scripts/gen_cert_headers.sh     # convert to C arrays
west build -b rpi_4b . --pristine
```

---

### SD Card / Boot Issues

#### No output on UART console
- Verify `arm_64bit=1` and `enable_uart=1` are in `config.txt`.
- Verify UART adapter RX is connected to RPi GPIO 14 (TX), not GPIO 15.
- Verify baud rate is exactly **115200**.
- Try a different USB-UART adapter (some cheap CH340 clones are unreliable).

#### `*** Booting Zephyr OS ***` appears but hangs
- Network initialization is taking time or failing. Verify the Ethernet cable is plugged in.
- Check `prj.conf` has `CONFIG_NET_DHCPV4=y`.

#### GPU rainbow screen / no boot
- The SD card is not formatted correctly (needs MBR, not GPT).
- `start4.elf` or `fixup4.dat` is missing or corrupted. Re-download.
- `bcm2711-rpi-4-b.dtb` is missing. Re-download.
- `config.txt` has a typo. Verify it exactly matches Section 10.

#### `zephyr.bin` not found by bootloader
- The filename in `config.txt` must exactly match the file on the SD card (`kernel=zephyr.bin`).
- The SD card partition must be FAT32 (not exFAT, not ext4).

---

### Network / TLS Issues

#### `[ERROR] Accept failed` on the server
The Ethernet interface may not have obtained an IP address yet. Watch the UART output and wait ~10 s after the `Listening on port 8080` line — DHCP negotiation can be slow.

#### `Connection refused` from the client
The server is not yet up. Wait for `[NETWORK] Waiting for connection...` in the UART output.

#### `SSL_connect` error on the client / `TLS Handshake failed` on the server
```
Cause 1: Clock skew — certificates appear expired if the host clock is wrong.
Fix: sudo ntpdate pool.ntp.org   (on host)

Cause 2: Wrong CA cert — client is using a CA that didn't sign the server cert.
Fix: Regenerate all certs with quick_start.sh and redistribute client.crt/key.

Cause 3: Client cert role mismatch — OU field doesn't match ADMIN/OPERATOR/etc.
Fix: Inspect the cert: openssl x509 -in certs/client.crt -noout -subject
```

#### `getsockopt(TLS_NATIVE) failed` in server log
The TLS handshake did not complete before `authorize_client()` was called. This can happen if the client disconnects immediately after the TCP handshake. It is not a bug — the server logs the error and moves on.

---

### Hardware / Sensor Issues

#### I2C device not initialising (`[HW] ERROR: I2C device not ready`)
- Verify SDA/SCL are connected to GPIO 2 / GPIO 3 (physical pins 3/5).
- Verify `&i2c1 { status = "okay"; }` is in `boards/rpi_4b.overlay`.
- Verify `CONFIG_I2C=y` is in `prj.conf`.
- Power cycle the ADS1115 board.

#### Current always reads 0 A
- The ACS712 requires **5 V** supply (not 3.3 V). Check Pin 2 of the RPi.
- Verify the ACS712 OUT pin is connected to ADS1115 channel A0.
- Check I2C is working: add `printk` inside `hw_read_current_i2c()` to log the raw ADC value.

#### Vibration / Sound always 0
- Verify the sensor DO pin is connected to the correct GPIO.
- Adjust the sensitivity potentiometer on the sensor module until the onboard LED reacts.
- Check `CONFIG_GPIO=y` is in `prj.conf`.

#### Temperature returns simulated value (25–26 °C regardless of real temp)
The full 1-Wire protocol is not yet implemented. The reset pulse is sent, but temperature reading is mocked. This is a known limitation — see [roadmap](#21-future-roadmap).

---

### Finding the RPi 4 IP Address

If DHCP assigned an IP but you don't know what it is, use any of these methods:

```bash
# 1. nmap ping scan of your subnet
nmap -sn 192.168.1.0/24

# 2. arp-scan (shows MAC addresses)
sudo arp-scan --localnet | grep -i "raspberry\|b8:27:eb\|dc:a6:32\|e4:5f:01"

# 3. Check your router's admin page
#    Usually at http://192.168.1.1 or http://192.168.0.1
```

To print the IP from Zephyr on boot, you can add to `apps/server.c` after DHCP completes:

```c
#include <zephyr/net/net_if.h>
struct net_if *iface = net_if_get_default();
struct in_addr *addr = &iface->config.ip.ipv4->unicast[0].address.in_addr;
printk("[NETWORK] IP: %d.%d.%d.%d\n",
       addr->s4_addr[0], addr->s4_addr[1],
       addr->s4_addr[2], addr->s4_addr[3]);
```

---

## 20. Project Architecture Deep-Dive

### Data flow from sensor to client

```
[GPIO pin (1 kHz)]
        │
        ▼
polling_thread()  in sensor_manager.c
        │  accumulates vib/sound counts for 1000 ms
        │  reads temp (1-Wire) and current (I2C) once per second
        │  evaluates HEALTHY / WARNING / CRITICAL
        │  locks data_mutex, writes current_health, unlocks
        │
        ▼
manager_get_health()   ← called from protocol.c handlers
        │  locks data_mutex, copies current_health, unlocks
        │
        ▼
cmd_get_sensors() / cmd_get_health() / cmd_monitor()
        │  formats string buffer
        │
        ▼
send_response()   →   zsock_send(tls_fd, ...)
        │
        ▼
[Encrypted TLS stream over Ethernet]
        │
        ▼
ims_client / dashboard.py on workstation
```

### Threading model

| Thread | Stack | Purpose |
|--------|-------|---------|
| `main` (Zephyr entry) | 8192 B | Hardware init, TLS setup, accept loop |
| `polling_thread` (pthread) | 4096 B | 1 kHz sensor polling + health evaluation |
| Per-client worker (pthread) | 4096 B | One per connected client (max 32) |

All threads use POSIX pthreads via Zephyr's POSIX compatibility layer (`CONFIG_PTHREAD_IPC=y`). Mutexes (`pthread_mutex_t`) protect `current_health` (sensor data) and `s_log_buf` (black-box ring buffer).

### TLS credential loading sequence

```
main()
  └─ setup_tls_credentials()
       ├─ tls_credential_add(TLS_CA_TAG,   ca_cert_der,     ...)
       ├─ tls_credential_add(TLS_CERT_TAG, server_cert_der, ...)
       └─ tls_credential_add(TLS_KEY_TAG,  server_key_der,  ...)

create_tls_listen_socket()
  ├─ zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2)
  ├─ zsock_setsockopt(SOL_TLS, TLS_SEC_TAG_LIST, {1,2,3})
  ├─ zsock_setsockopt(SOL_TLS, TLS_PEER_VERIFY, REQUIRED)
  ├─ zsock_bind(...)
  └─ zsock_listen(...)

accept loop:
  └─ zsock_accept()          ← TLS handshake on first I/O
       └─ authorize_client(client_fd)
            ├─ getsockopt(TLS_NATIVE) → mbedtls_ssl_context*
            ├─ mbedtls_ssl_get_peer_cert()
            └─ walk X.509 subject for CN + OU → UserRole
```

---

## 21. Future Roadmap

| Status | Item |
|--------|------|
| ✅ Done | Zephyr RTOS migration from QNX |
| ✅ Done | Zephyr GPIO driver for vibration + sound sensors |
| ✅ Done | Zephyr I2C driver for ADS1115 + ACS712 current sensor |
| ✅ Done | Zephyr TLS server with MbedTLS (replaces OpenSSL) |
| ✅ Done | MbedTLS X.509 peer cert parsing for RBAC |
| ✅ Done | In-memory black-box ring buffer (replaces file I/O) |
| 🔄 In progress | Full DS18B20 1-Wire protocol (bit-bang ROM commands) |
| 🔄 In progress | Python dashboard stability and feature completion |
| 📋 Planned | LittleFS on SPI flash for persistent black-box log |
| 📋 Planned | Print assigned DHCP IP on UART boot output |
| 📋 Planned | Zephyr shell integration for on-device diagnostics |
| 📋 Planned | OTA firmware update via Zephyr MCUboot |
| 📋 Planned | CSV export of historical sensor data |
| 📋 Planned | Web-based monitoring dashboard |

---

## Quick Reference Card

```
┌─────────────────────────────────────────────────────────────┐
│                 SENTINEL-RT QUICK REFERENCE                 │
├─────────────────────────────────────────────────────────────┤
│ FIRST TIME SETUP                                            │
│  1. Install Zephyr SDK:  ~/zephyr-sdk-X.Y.Z/setup.sh       │
│  2. Init workspace:      west init ~/zephyrproject          │
│                          cd ~/zephyrproject && west update  │
│  3. Clone project into workspace                            │
│                                                             │
│ EVERY BUILD CYCLE                                           │
│  1. Generate certs:      ./scripts/quick_start.sh certs    │
│  2. Convert to headers:  ./scripts/gen_cert_headers.sh     │
│  3. Build Zephyr image:  west build -b rpi_4b .            │
│  4. Copy to SD card:     cp build/zephyr/zephyr.bin /mnt/  │
│  5. Build Linux client:  make client_linux                  │
│                                                             │
│ SD CARD CONTENTS                                            │
│  bcm2711-rpi-4-b.dtb  start4.elf  fixup4.dat               │
│  zephyr.bin           config.txt                            │
│                                                             │
│ config.txt                                                  │
│  kernel=zephyr.bin                                          │
│  arm_64bit=1                                                │
│  enable_uart=1                                              │
│  uart_2ndstage=1                                            │
│                                                             │
│ UART CONSOLE: GPIO14(TX)→Adapter RX, 115200 baud           │
│  picocom -b 115200 /dev/ttyUSB0                             │
│                                                             │
│ CONNECT CLIENT                                              │
│  ./ims_client <RPi4_IP>                                     │
│  python3 clients/dashboard.py                               │
└─────────────────────────────────────────────────────────────┘
```

---

**Author:** Hemanth Kumar
**GitHub:** [@Hemanthkumar04](https://github.com/Hemanthkumar04)
**Email:** hky21.github@gmail.com
**Project Type:** Final Year Project — Industrial Equipment Health Monitoring System (IEHMS)
**License:** Academic / Open Source
