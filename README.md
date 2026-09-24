# Replay emulator plugin SDK

Everything an emulator plugin for Replay builds against. An emulator plugin is a shared
library emulating one system; the host loads it, drives it one frame at a time, and shows
what it draws. This tree is staged by the replay_frontend build and published unedited;
`UPSTREAM` names the host commit it came from.

```
include/     the plugin ABI (replay/emu_plugin.h) and the flowi headers it needs: arenas, strings, logging
exports/     host_exports.txt: the host symbols a plugin may call, one per line
cmake/       ReplaySDK.cmake, the helper a plugin's CMakeLists includes
scripts/     check_plugin.sh, the import audit for a built plugin
examples/    stub-emu, a complete plugin with no emulator behind it
```

## Writing a plugin

```c
#include <replay/emu_plugin.h>

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &my_api;
}

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()
```

`rp_emu_plugin_get` returns the vtable the host drives. `RP_EMU_PLUGIN_ABI_VERSION_EXPORT()`
exports the ABI version the plugin was built against; the host rejects a plugin whose version
differs from its own before it reads the vtable. `examples/stub-emu` fills every required slot.

```cmake
set(REPLAY_SDK_DIR /path/to/replay_emulator_sdk)
include(${REPLAY_SDK_DIR}/cmake/ReplaySDK.cmake)
add_replay_emu_plugin(NAME my_system SOURCES my_system.c)
```

The output is `my_system.so` (`.dylib` on macOS), with no `lib` prefix. It is installed beside
the config template whose `plugin` field names it.

## Calling the host

A plugin links nothing of the host. Its `arena_*`, `string_*` and `fl_log_*` calls stay
undefined and the dynamic loader binds them to the host executable at load. The link step
therefore cannot catch a call the host does not offer, so `scripts/check_plugin.sh` does: it
fails a plugin that imports a symbol missing from `exports/host_exports.txt`, or that defines
one of them itself. The CMake helper runs it after every link.

The export list is only the symbols these headers declare. A plugin calling any other host
function is rejected, even when the host happens to export it.

## ABI version

`RP_PLUGIN_ABI_VERSION` covers exactly this SDK: the headers under `include/` and the export
list. The host's other plugin APIs move without touching it, so a published plugin keeps
loading until this surface changes. The host's CI fails when it changes without a version bump.

## Building the example

```bash
cmake -S examples/stub-emu -B out && cmake --build out
```

## Platforms

Linux and macOS.

## License

MIT, see `LICENSE`.
