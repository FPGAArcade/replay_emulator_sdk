# Replay plugin SDK - CMake helper for C plugins.
#
#   set(REPLAY_SDK_DIR /path/to/sdk)          # the staged SDK: include/, exports/, cmake/, scripts/, rust/, examples/
#   include(${REPLAY_SDK_DIR}/cmake/ReplaySDK.cmake)
#   add_replay_ui_plugin(NAME my_plugin SOURCES my_plugin.c)
#   add_replay_emu_plugin(NAME my_system SOURCES my_system.c)
#
# A plugin links nothing of the host. Its fl_*/arena_*/vfs_* calls and its rp_* calls alike
# stay undefined on purpose: the host executable defines and exports all of them, and the
# dynamic loader resolves them when the plugin is loaded. That is why no --no-undefined is
# applied here - a plugin cannot be link-checked against a host it never links. The check
# that replaces it is scripts/check_plugin.sh, run on the built plugin against the export
# list the SDK ships (exports/host_exports.txt); the helper adds it as a post-build step.
#
# The output is <name>.so / <name>.dylib with no lib prefix, which is the filename the host
# looks for: under plugins/ui/<name>/ for a UI plugin, and named by the "plugin" field of the
# config template beside it for an emulator plugin.

if(NOT REPLAY_SDK_DIR)
    message(FATAL_ERROR "ReplaySDK.cmake: set REPLAY_SDK_DIR to the staged SDK directory before including it")
endif()
if(NOT IS_DIRECTORY "${REPLAY_SDK_DIR}/include/replay")
    message(FATAL_ERROR "ReplaySDK.cmake: '${REPLAY_SDK_DIR}' does not look like a staged SDK (no include/replay)")
endif()
if(NOT EXISTS "${REPLAY_SDK_DIR}/exports/host_exports.txt")
    message(FATAL_ERROR "ReplaySDK.cmake: no host export list at '${REPLAY_SDK_DIR}/exports/host_exports.txt'")
endif()

# The shared body behind the two public names below: a UI plugin and an emulator plugin build
# identically, and differ only in where the host loads the result from.
function(_replay_add_host_symbol_plugin)
    cmake_parse_arguments(PLUGIN "" "NAME" "SOURCES" ${ARGN})
    if(NOT PLUGIN_NAME OR NOT PLUGIN_SOURCES)
        message(FATAL_ERROR "add_replay_ui_plugin/add_replay_emu_plugin: NAME and SOURCES are required")
    endif()

    add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SOURCES})
    set_target_properties(${PLUGIN_NAME} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${PLUGIN_NAME}"
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${PLUGIN_NAME} PRIVATE "${REPLAY_SDK_DIR}/include")
    if(APPLE)
        # ld64 errors on undefined symbols by default; the host resolves them at load.
        target_link_options(${PLUGIN_NAME} PRIVATE -Wl,-undefined,dynamic_lookup)
    endif()
    # The sysroot goes with it because a cross-built plugin's libc is the sysroot's, not this
    # machine's, and the check has no other way to know where to read it from. It is empty for
    # a native build, which is what tells check_plugin.sh to ask the loader instead.
    add_custom_command(TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E env "REPLAY_CHECK_SYSROOT=${CMAKE_SYSROOT}"
            "${REPLAY_SDK_DIR}/scripts/check_plugin.sh" "$<TARGET_FILE:${PLUGIN_NAME}>"
        COMMENT "Checking ${PLUGIN_NAME}'s imports against the host export list"
        VERBATIM)
endfunction()

# A UI plugin: exports rp_ui_plugin_get, installed as plugins/ui/<name>/<name>.so.
function(add_replay_ui_plugin)
    _replay_add_host_symbol_plugin(${ARGN})
endfunction()

# An emulator plugin: exports rp_emu_plugin_get, installed beside the config template that
# names it.
function(add_replay_emu_plugin)
    _replay_add_host_symbol_plugin(${ARGN})
endfunction()
