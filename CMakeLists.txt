cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(rfid-zephyr-rtos)

add_subdirectory(drivers/mfrc522)

target_sources(app PRIVATE
    src/main.c
)