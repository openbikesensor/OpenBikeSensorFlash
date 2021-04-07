cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# target_add_binary_data(firmware.bin "main/sd-partition-table.bin" BINARY)
project(esp32-sdflash-pio)
