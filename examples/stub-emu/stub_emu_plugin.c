// A deterministic emulator plugin with no emulator behind it. It synthesizes a framebuffer and an
// audio ramp from a frame counter, so a given frame index always produces the same bytes, and it
// declares every RpEmuCapability with the slots each one needs, so it is the reference for what a
// complete plugin fills in:
//
//   RpEmuInfo.capabilities     the OSD items the core backs, each with its slots below
//   RpEmuInfo.digital_actions  the Actions the core reads, with its own label for each
//   run_frame                  reads Port 1's Action state from ctx->input->ports[0]
//
// The RP_STUB_EMU_* defines build deliberately broken variants for Replay's loader tests; a
// plugin built from this file leaves them all undefined.

#include <flowi/arena/arena_macros.h>
#include <flowi/core/log_macros.h>
#include <flowi/core/types.h>
#include <flowi/string/string.h>
#include <replay/emu_plugin.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define STUB_EMU_WIDTH 320
#define STUB_EMU_HEIGHT 240
#define STUB_EMU_FPS 50
#define STUB_EMU_SAMPLE_RATE 48000
#define STUB_EMU_CHANNELS 2
// One video frame's worth of audio, which divides exactly at these two rates.
#define STUB_EMU_FRAMES_PER_CALL (STUB_EMU_SAMPLE_RATE / STUB_EMU_FPS)
// The one drive get_media_slots reports, and the sides of what is inserted in it.
#define STUB_EMU_DRIVE 0
#define STUB_EMU_DRIVES 1
#define STUB_EMU_SIDES 2
// Every blue bit, flipped while Port 1 holds Action.
#define STUB_EMU_BLUE_MASK 0xffu

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// What serialize writes: the state run_frame's output depends on.
typedef struct StubEmuState {
    u32 frame_index;
    u32 media_side;
} StubEmuState;

typedef struct StubEmuCore {
    u32 pixels[STUB_EMU_WIDTH * STUB_EMU_HEIGHT];
    f32 samples[STUB_EMU_FRAMES_PER_CALL * STUB_EMU_CHANNELS];
    // Advanced by run_frame and reset to zero by reset()/hard_reset(); the sole input to both
    // generators, which is what makes a frame index reproduce its bytes.
    u32 frame_index;
    bool media_mounted;
    // What is in the drive, and which of its sides is in use.
    FlString media_path;
    u32 media_side;
    // The last word set_config_bitmask wrote.
    u32 config_bitmask;
    FlArena* arena;
    // Returned by get_config_string: where the host said the ROMs are.
    FlString bios_directory;
} StubEmuCore;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const RpEmuDigitalActionLabel STUB_EMU_DIGITAL_ACTIONS[] = {
    { RpDigitalAction_Up, "Up" },
    { RpDigitalAction_Down, "Down" },
    { RpDigitalAction_Left, "Left" },
    { RpDigitalAction_Right, "Right" },
    { RpDigitalAction_Action, "Fire" },
    { RpDigitalAction_Start, "Pause" },
};

static const RpEmuAnalogActionLabel STUB_EMU_ANALOG_ACTIONS[] = {
    { RpAnalogAction_LeftStickX, "Steer" },
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_get_info(RpEmuInfo* info) {
    info->emu_name = "Stub Emu";
    info->emu_version = "1.0";
    info->system_name = "Stub System";
    info->supported_extensions = "stub";
    info->requires_bios = false;
    info->capabilities = RpEmuCapability_SaveState | RpEmuCapability_SoftReset | RpEmuCapability_HardReset |
                         RpEmuCapability_Media | RpEmuCapability_DipSwitches;
    info->digital_actions = STUB_EMU_DIGITAL_ACTIONS;
    info->digital_action_count = (u32)(sizeof(STUB_EMU_DIGITAL_ACTIONS) / sizeof(STUB_EMU_DIGITAL_ACTIONS[0]));
    info->analog_actions = STUB_EMU_ANALOG_ACTIONS;
    info->analog_action_count = (u32)(sizeof(STUB_EMU_ANALOG_ACTIONS) / sizeof(STUB_EMU_ANALOG_ACTIONS[0]));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* stub_emu_create(FlArena* arena) {
    StubEmuCore* core = arena_alloc_zero(arena, StubEmuCore);
    core->arena = arena;
    fl_log_info("Stub emu core created");
    return core;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_destroy(void* core_instance) {
    // The instance is arena memory the host owns; it goes when the arena does.
    UNUSED(core_instance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// There is no media to read, so any path is accepted except one naming an entry that starts with "reject", which
// lets a test drive a core refusing media.

static bool stub_emu_insert(StubEmuCore* core, FlString path) {
    if (string_find_substr(path, S("/reject")) >= 0) {
        return false;
    }
    core->media_path = string_copy(core->arena, path);
    core->media_side = 0;
    core->media_mounted = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_eject(StubEmuCore* core) {
    core->media_path = string_empty();
    core->media_side = 0;
    core->media_mounted = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool stub_emu_mount_media(void* core_instance, const char* path) {
    return stub_emu_insert((StubEmuCore*)core_instance, string_from_cstr(path));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_unmount_media(void* core_instance) {
    stub_emu_eject((StubEmuCore*)core_instance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef RP_STUB_EMU_OMIT_RUN_FRAME

static void stub_emu_run_frame(void* core_instance, RpEmuFrameContext* ctx) {
    StubEmuCore* core = (StubEmuCore*)core_instance;

    // Port 1's Action state, already through the player's Mapping: Fire inverts the blue channel, so a
    // test can see input arrive.
    const RpEmuPortInput* port = &ctx->input->ports[0];
    const u32 blue_flip = port->digital[RpDigitalAction_Action] ? STUB_EMU_BLUE_MASK : 0;

    // A diagonal gradient scrolling with the frame counter: every channel varies across both axes
    // and over time, so a stuck row, column or frame shows up as a byte difference.
    for (u32 y = 0; y < STUB_EMU_HEIGHT; y++) {
        for (u32 x = 0; x < STUB_EMU_WIDTH; x++) {
            const u32 red = (x + core->frame_index) & 0xff;
            const u32 green = (y + core->frame_index) & 0xff;
            const u32 blue = ((x + y + core->frame_index) & 0xff) ^ blue_flip;
            core->pixels[(y * STUB_EMU_WIDTH) + x] = (red << 16) | (green << 8) | blue;
        }
    }

    // A sawtooth over the frame, offset per channel so a swapped pair is visible.
    for (u32 frame = 0; frame < STUB_EMU_FRAMES_PER_CALL; frame++) {
        const f32 phase = (f32)((frame + core->frame_index) % STUB_EMU_FRAMES_PER_CALL);
        const f32 value = (phase / (f32)STUB_EMU_FRAMES_PER_CALL) - 0.5f;
        core->samples[frame * STUB_EMU_CHANNELS] = value;
        core->samples[(frame * STUB_EMU_CHANNELS) + 1] = -value;
    }

    core->frame_index++;

    ctx->video_buffer = core->pixels;
    ctx->video_pitch = STUB_EMU_WIDTH * (u32)sizeof(u32);
    ctx->video_width = STUB_EMU_WIDTH;
    ctx->video_height = STUB_EMU_HEIGHT;
    ctx->audio_buffer = core->samples;
    ctx->audio_frames = STUB_EMU_FRAMES_PER_CALL;
}

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_reset(void* core_instance) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    core->frame_index = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A power cycle, so the inserted media stays in the drive.
static void stub_emu_hard_reset(void* core_instance) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    core->frame_index = 0;
    core->media_side = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_get_video_spec(void* core_instance, RpVideoSpec* spec) {
    UNUSED(core_instance);
    spec->width = STUB_EMU_WIDTH;
    spec->height = STUB_EMU_HEIGHT;
    spec->pixel_format = RpPixelFormat_Xrgb8888;
    spec->fps = (f32)STUB_EMU_FPS;
    spec->visible_x = 0;
    spec->visible_y = 0;
    spec->visible_width = STUB_EMU_WIDTH;
    spec->visible_height = STUB_EMU_HEIGHT;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_get_audio_spec(void* core_instance, RpAudioSpec* spec) {
    UNUSED(core_instance);
    spec->sample_rate = STUB_EMU_SAMPLE_RATE;
    spec->channels = STUB_EMU_CHANNELS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuLifecycleState stub_emu_get_state(void* core_instance) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    return core->media_mounted ? RpEmuLifecycleState_Running : RpEmuLifecycleState_Idle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t stub_emu_serialize(void* core_instance, void* buffer, size_t buffer_size) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    if (buffer == nullptr) {
        return sizeof(StubEmuState);
    }
    if (buffer_size < sizeof(StubEmuState)) {
        fl_log_error("Stub emu: a %u byte state does not fit %u bytes", (u32)sizeof(StubEmuState), (u32)buffer_size);
        return 0;
    }
    StubEmuState* state = (StubEmuState*)buffer;
    state->frame_index = core->frame_index;
    state->media_side = core->media_side;
    return sizeof(StubEmuState);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef RP_STUB_EMU_OMIT_DESERIALIZE

static bool stub_emu_deserialize(void* core_instance, const void* buffer, size_t buffer_size) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    if (buffer_size != sizeof(StubEmuState)) {
        fl_log_error("Stub emu: a state is %u bytes, not %u", (u32)sizeof(StubEmuState), (u32)buffer_size);
        return false;
    }
    const StubEmuState* state = (const StubEmuState*)buffer;
    core->frame_index = state->frame_index;
    core->media_side = state->media_side;
    return true;
}

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_set_config_bitmask(void* core_instance, u32 config_value) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    core->config_bitmask = config_value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 stub_emu_get_media_slots(void* core_instance, RpEmuMediaSlot* slots, u32 max_slots) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    if (max_slots > STUB_EMU_DRIVE) {
        slots[STUB_EMU_DRIVE] = (RpEmuMediaSlot){
            .label = S("Stub drive"),
            .inserted_path = core->media_path,
            .has_sides = core->media_mounted,
        };
    }
    return STUB_EMU_DRIVES;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool stub_emu_set_media(void* core_instance, u32 slot, FlString path) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    if (slot != STUB_EMU_DRIVE) {
        return false;
    }
    if (path.length == 0) {
        stub_emu_eject(core);
        return true;
    }
    return stub_emu_insert(core, path);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool stub_emu_next_side(void* core_instance, u32 slot) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    if (slot != STUB_EMU_DRIVE || !core->media_mounted) {
        return false;
    }
    core->media_side = (core->media_side + 1) % STUB_EMU_SIDES;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef RP_STUB_EMU_OMIT_CONFIG_STRING

static void stub_emu_set_config_string(void* core_instance, FlString key, FlString value) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    if (string_equals(key, S("BIOS_DIRECTORY"))) {
        core->bios_directory = string_copy(core->arena, value);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString stub_emu_get_config_string(void* core_instance, FlString key) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    if (string_equals(key, S("BIOS_DIRECTORY"))) {
        return core->bios_directory;
    }
    return string_empty();
}

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static RpEmuAPI stub_emu_api = {
    .get_info = stub_emu_get_info,
    .create = stub_emu_create,
    .destroy = stub_emu_destroy,
    .mount_media = stub_emu_mount_media,
    .unmount_media = stub_emu_unmount_media,
#ifndef RP_STUB_EMU_OMIT_RUN_FRAME
    .run_frame = stub_emu_run_frame,
#endif
    .reset = stub_emu_reset,
    .hard_reset = stub_emu_hard_reset,
    .get_video_spec = stub_emu_get_video_spec,
    .get_audio_spec = stub_emu_get_audio_spec,
    .get_state = stub_emu_get_state,
    .serialize = stub_emu_serialize,
#ifndef RP_STUB_EMU_OMIT_DESERIALIZE
    .deserialize = stub_emu_deserialize,
#endif
    .set_config_bitmask = stub_emu_set_config_bitmask,
    .get_media_slots = stub_emu_get_media_slots,
    .set_media = stub_emu_set_media,
    .next_side = stub_emu_next_side,
#ifndef RP_STUB_EMU_OMIT_CONFIG_STRING
    .set_config_string = stub_emu_set_config_string,
    .get_config_string = stub_emu_get_config_string,
#endif
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void);

RP_EMU_EXPORT const RpEmuAPI* rp_emu_plugin_get(void) {
    return &stub_emu_api;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(RP_STUB_EMU_REPORTED_ABI_VERSION)

// Spelled out rather than through RP_EMU_PLUGIN_ABI_VERSION_EXPORT(), because the point of these
// variants is to disagree with the host's constant.

RP_EMU_EXPORT uint64_t rp_emu_plugin_abi_version(void) {
    return RP_STUB_EMU_REPORTED_ABI_VERSION;
}

#elif !defined(RP_STUB_EMU_OMIT_ABI_VERSION)

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
