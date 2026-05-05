# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(haykulFinderFirmware)

target_sources(app PRIVATE
    src/main.cpp
    src/ble_manager.cpp
    src/buzzer.cpp
    src/imu_bma400.cpp
    src/nfc_manager.cpp
    src/button_manager.cpp
    src/led_manager.cpp
    src/battery_monitor.cpp
)