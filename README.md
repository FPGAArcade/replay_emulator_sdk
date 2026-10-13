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

The types are in `include/replay/plugin/emu_plugin_types.h`, the Action vocabulary in
`include/replay/plugin/action_types.h`. Both are generated, and their comments are the
reference for every field and slot.

## Writing a plugin

```c
#include <replay/emu_plugin.h>

static const RpEmuAPI s_api = { /* ... */ };

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &s_api;
}

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()
```

`get_info`, `create`, `destroy`, `mount_media` and `run_frame` are always required. Arenas,
strings and logging come from `flowi/arena/arena_macros.h`, `flowi/string/string.h` and
`flowi/core/log_macros.h`. `examples/stub-emu` fills in every slot and declares every
capability.

```cmake
set(REPLAY_SDK_DIR /path/to/replay_emulator_sdk)
include(${REPLAY_SDK_DIR}/cmake/ReplaySDK.cmake)
add_replay_emu_plugin(NAME my_system SOURCES my_system.c)
```

This builds `my_system.so` (`.dylib` on macOS) with no `lib` prefix. Install it next to the
config template whose `plugin` field names it.

## What the host checks at load

1. `rp_emu_plugin_abi_version()` must return the host's `RP_PLUGIN_ABI_VERSION`. Nothing else
   is read from a plugin that fails this.
2. `rp_emu_plugin_get()` must return a vtable with the five required slots.
3. `get_info` is called once, before `create`. The plugin is refused when a capability bit is
   set without its slots, when an unknown bit is set, or when a declared Action is one a core
   is not fed.

## Capabilities

`RpEmuInfo.capabilities` is a fixed bitmask of `RpEmuCapability` values. It says which items
the in-game menu (the OSD) offers for the core; an item for an unset bit is left out, not
greyed. Declare a bit only when it works as described:

| Bit | Means | Slots it needs |
|-----|-------|----------------|
| `RpEmuCapability_SaveState` | save and load state | `serialize`, `deserialize` |
| `RpEmuCapability_SoftReset` | warm boot, keeping RAM and media | `reset` |
| `RpEmuCapability_HardReset` | power cycle; mounted media stays mounted | `hard_reset` |
| `RpEmuCapability_Media` | eject, insert and flip media per drive | `get_media_slots`, `set_media`, `next_side` |
| `RpEmuCapability_DipSwitches` | DIP switches as core options in the config word | `set_config_bitmask` |

A slot may be filled without its bit set; the host only offers what the bits say.

```c
info->capabilities = RpEmuCapability_HardReset | RpEmuCapability_SaveState;
```

## Input: Actions per Port

A core never sees a pad button. Each frame `ctx->input` carries `ports[RP_EMU_MAX_PORTS]`,
Port 1 first, and each `RpEmuPortInput` is already read through the player's own Mapping:

- `digital[RP_EMU_DIGITAL_ACTIONS]`, indexed by `RpDigitalAction`, true while held. Menu is
  host-reserved, so the array stops before it.
- `analog[RP_EMU_ANALOG_ACTIONS]`, indexed by `RpAnalogAction`. Sticks run from -32767
  (left, up) to 32767 (right, down); triggers from 0 to 32767.

```c
const RpEmuPortInput* port = &ctx->input->ports[0];
bool fire = port->digital[RpDigitalAction_Action];
i16 steer = port->analog[RpAnalogAction_LeftStickX];
```

`RpDigitalAction_Action` is the primary face button (fire in a game), `Secondary` the next,
then `Third` and `Fourth`. Keyboard and mouse are unchanged: `keyboard_keys`, `mouse_x`,
`mouse_y`, `mouse_left`, `mouse_right`.

## Declared Actions

A core lists the Actions it reads, each with its own label, through `RpEmuInfo`. The button
legend and the controller wizard show these labels. Every Action is still delivered whether
declared or not; an undeclared one simply has no label. The lists are the core's own and must
stay valid while it is loaded, so make them static:

```c
static const RpEmuDigitalActionLabel s_digital[] = {
    { RpDigitalAction_Action, "Fire" },
    { RpDigitalAction_Start, "Pause" },
};

info->digital_actions = s_digital;
info->digital_action_count = sizeof(s_digital) / sizeof(s_digital[0]);
// and the same for analog_actions / analog_action_count with RpEmuAnalogActionLabel
```

`RpDigitalAction_Menu` cannot be declared.

## Media slots

With `RpEmuCapability_Media` set, the OSD lists the core's drives and offers Eject, Insert and
Next side on each:

- `get_media_slots(core, slots, max_slots)` fills up to `max_slots` `RpEmuMediaSlot` entries
  (label, inserted path, has sides) and returns how many drives the core has. Its strings stay
  valid until the next call into the core.
- `set_media(core, slot, path)` inserts `path` into drive `slot`, or ejects it when `path` is
  empty.
- `next_side(core, slot)` turns to the next side or disk of what is in drive `slot`, wrapping
  after the last, and returns false when there is no other side.

`mount_media` and `mount_media_list` stay the launch-time entry points.

## Porting from ABI version 1

- Rebuild against this SDK; a version 1 plugin is refused at load.
- Replace every `RpEmuButton` read with the `RpDigitalAction` it meant, from
  `ctx->input->ports[n].digital` (`players` and the top-level `buttons` are gone). The old
  `A`, `B`, `X`, `Y`, `L`, `R` are `Action`, `Secondary`, `Third`, `Fourth`, `ShoulderL`,
  `ShoulderR`. Analog sticks and triggers are new, in `ports[n].analog`.
- Set `capabilities` to what the wrapper honestly backs, and fill the slots each bit needs.
- Declare the Actions the core reads, with their labels.
- Leave `RpEmuCapability_Media` unset until `get_media_slots`, `set_media` and `next_side` are
  implemented.

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
