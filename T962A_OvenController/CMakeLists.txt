cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(OvenController)

add_custom_command(TARGET OvenController.elf POST_BUILD
    COMMAND python3 ${CMAKE_SOURCE_DIR}/rename_firmware.py ${CMAKE_BINARY_DIR}
    COMMENT "Renaming firmware binary..."
)
