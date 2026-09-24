# Replay emulator plugin SDK

Headers and build glue for writing emulator plugins for Replay. A plugin is a shared library
that emulates one system; the host drives it one frame at a time. `UPSTREAM` names the Replay
commit this tree was generated from, so don't edit it by hand.

```
include/     replay/emu_plugin.h and the flowi headers it uses
exports/     host_exports.txt, the host symbols a plugin may call
cmake/       ReplaySDK.cmake
scripts/     check_plugin.sh, the import check for a built plugin
examples/    stub-emu, a complete plugin with no emulator behind it
```

## Writing a plugin

```c
#include <replay/emu_plugin.h>

static const RpEmuAPI s_api = { /* ... */ };

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_api;
}

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()
```

The host checks the exported ABI version before it reads the vtable, and refuses a mismatch.
Arenas, strings and logging come from `flowi/arena/arena_macros.h`, `flowi/string/string.h`
and `flowi/core/log_macros.h`. `examples/stub-emu` fills in every required slot.

```cmake
set(REPLAY_SDK_DIR /path/to/replay_emulator_sdk)
include(${REPLAY_SDK_DIR}/cmake/ReplaySDK.cmake)
add_replay_emu_plugin(NAME my_system SOURCES my_system.c)
```

This builds `my_system.so` (`.dylib` on macOS) with no `lib` prefix. Install it next to the
config template whose `plugin` field names it.

## Host calls

A plugin doesn't link against the host. The loader binds its host calls when it is loaded,
so a missing symbol isn't caught at link time. `scripts/check_plugin.sh` catches it instead:
it runs after every link and fails if the plugin imports anything not listed in
`exports/host_exports.txt`, or defines a symbol from that list itself.

## ABI version

`RP_PLUGIN_ABI_VERSION` changes only when the headers or the export list change, so a
built plugin keeps loading across host releases until then.

## Building the example

```bash
cmake -S examples/stub-emu -B out && cmake --build out
```

Linux and macOS. MIT licensed, see `LICENSE`.
