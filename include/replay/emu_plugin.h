#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Replay Emulator Plugin API
//
// This API is inspired by libretro but tailored to the Replay Frontend architecture.
// An emulator plugin is a dynamically loaded shared library emulating one system.
//
// Example plugins: Amiga (vAmiga), C64, NES, Genesis, etc.
//
// The types come from <replay/plugin/emu_plugin_types.h>. What lives here are the pieces the IDL has no
// construct for: the entry-point visibility macro, the ABI version export the host gates on, and the
// checks tying the input arrays back to their named bounds.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include <flowi/core/string.h>
#include <replay/plugin/emu_plugin_types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input array bounds
//
// The IDL spells the button and player array lengths as literals, so pin them against the enum sentinel and
// the player count they are meant to track.

static_assert(sizeof(((RpEmuPlayerInput*)0)->buttons) / sizeof(bool) == RpEmuButton_Count,
              "RpEmuPlayerInput.buttons must be RpEmuButton_Count long");
static_assert(sizeof(((RpEmuInputState*)0)->buttons) / sizeof(bool) == RpEmuButton_Count,
              "RpEmuInputState.buttons must be RpEmuButton_Count long");
static_assert(sizeof(((RpEmuInputState*)0)->players) / sizeof(RpEmuPlayerInput) == RP_EMU_MAX_PLAYERS,
              "RpEmuInputState.players must be RP_EMU_MAX_PLAYERS long");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Emulator plugin entry point - must be exported by the shared library
//
// Example:
//   RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void);

#ifdef _WIN32
#define RP_EMU_EXPORT __declspec(dllexport)
#else
#define RP_EMU_EXPORT __attribute__((visibility("default")))
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ABI version export - must be exported by the shared library
//
// The host resolves this before it reads the vtable and rejects the plugin when the answer is not
// RP_PLUGIN_ABI_VERSION, so a plugin built against a different ABI is turned away rather than
// called into. Place RP_EMU_PLUGIN_ABI_VERSION_EXPORT() at file scope in exactly one translation
// unit of the plugin.

RP_EMU_EXPORT uint64_t rp_emu_plugin_abi_version(void);

#define RP_EMU_PLUGIN_ABI_VERSION_EXPORT()                   \
    RP_EMU_EXPORT uint64_t rp_emu_plugin_abi_version(void) { \
        return RP_PLUGIN_ABI_VERSION;                        \
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
