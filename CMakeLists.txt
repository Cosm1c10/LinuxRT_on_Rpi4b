cmake_minimum_required(VERSION 3.20.0)

# Zephyr workspace integration: set ZEPHYR_BASE or use west
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sentinel_rt VERSION 1.0.0)

# ===========================================================================
# Server application — compiled for Zephyr target (rpi_4b)
# The Linux client (apps/client.c) is built separately via the Makefile.
# ===========================================================================
target_sources(app PRIVATE
    apps/server.c
    common/authorization.c
    drivers/sensors.c
    drivers/sensor_manager.c
    protocol/protocol.c
)

target_include_directories(app PRIVATE
    apps
    common
    drivers
    protocol
)
