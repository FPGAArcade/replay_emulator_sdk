// A deterministic emulator plugin with no emulator behind it. It fills the RpEmuAPI slots that are
// not optional and synthesizes a framebuffer and an audio ramp from a frame counter, so a given
// frame index always produces the same bytes.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct StubEmuCore {
    u32 pixels[STUB_EMU_WIDTH * STUB_EMU_HEIGHT];
    f32 samples[STUB_EMU_FRAMES_PER_CALL * STUB_EMU_CHANNELS];
    // Advanced by run_frame and reset to zero by reset()/hard_reset(); the sole input to both
    // generators, which is what makes a frame index reproduce its bytes.
    u32 frame_index;
    bool media_mounted;
    FlArena* arena;
    // Returned by get_config_string: where the host said the ROMs are.
    FlString bios_directory;
} StubEmuCore;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_get_info(RpEmuInfo* info) {
    info->emu_name = "Stub Emu";
    info->emu_version = "1.0";
    info->system_name = "Stub System";
    info->supported_extensions = "stub";
    info->requires_bios = false;
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

// Accepts any path: there is no media to read.

static bool stub_emu_mount_media(void* core_instance, const char* path) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    UNUSED(path);
    core->media_mounted = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void stub_emu_unmount_media(void* core_instance) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    core->media_mounted = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef RP_STUB_EMU_OMIT_RUN_FRAME

static void stub_emu_run_frame(void* core_instance, RpEmuFrameContext* ctx) {
    StubEmuCore* core = (StubEmuCore*)core_instance;

    // A diagonal gradient scrolling with the frame counter: every channel varies across both axes
    // and over time, so a stuck row, column or frame shows up as a byte difference.
    for (u32 y = 0; y < STUB_EMU_HEIGHT; y++) {
        for (u32 x = 0; x < STUB_EMU_WIDTH; x++) {
            const u32 red = (x + core->frame_index) & 0xff;
            const u32 green = (y + core->frame_index) & 0xff;
            const u32 blue = (x + y + core->frame_index) & 0xff;
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

static void stub_emu_hard_reset(void* core_instance) {
    StubEmuCore* core = (StubEmuCore*)core_instance;
    core->frame_index = 0;
    core->media_mounted = false;
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

#if defined(RP_STUB_EMU_WRONG_ABI_VERSION)

// Spelled out rather than through RP_EMU_PLUGIN_ABI_VERSION_EXPORT(), because the point of this
// variant is to disagree with the host's constant.

RP_EMU_EXPORT uint64_t rp_emu_plugin_abi_version(void) {
    return RP_PLUGIN_ABI_VERSION + 1;
}

#elif !defined(RP_STUB_EMU_OMIT_ABI_VERSION)

RP_EMU_PLUGIN_ABI_VERSION_EXPORT()

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
