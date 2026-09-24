cmake_minimum_required(VERSION 3.16)
project(replay_emu_sdk_stub C)

# Point REPLAY_SDK_DIR at a staged SDK (-DREPLAY_SDK_DIR=...). Unset, it assumes this
# example still sits inside the SDK it shipped with.
if(NOT REPLAY_SDK_DIR)
    get_filename_component(REPLAY_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
include("${REPLAY_SDK_DIR}/cmake/ReplaySDK.cmake")

add_replay_emu_plugin(NAME stub_emu SOURCES stub_emu_plugin.c)
target_compile_options(stub_emu PRIVATE -Wall -Wextra)
