#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef TARGET_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include "sm64.h"

#include "pc/lua/smlua.h"

#include "game/memory.h"
#include "audio/external.h"

#include "network/network.h"
#include "lua/smlua.h"

#include "gfx/gfx_pc.h"

#include "gfx/gfx_opengl.h"
#include "gfx/gfx_direct3d11.h"
#include "gfx/gfx_direct3d12.h"

#include "gfx/gfx_dxgi.h"
#include "gfx/gfx_sdl.h"
#include "gfx/gfx_dummy.h"

#include "audio/audio_api.h"
#include "audio/audio_sdl.h"
#include "audio/audio_null.h"

#include "pc_main.h"
#include "cliopts.h"
#include "configfile.h"
#include "controller/controller_api.h"
#include "controller/controller_keyboard.h"
#include "fs/fs.h"

#include "game/game_init.h"
#include "game/main.h"
#include "game/thread6.h"

#ifdef DISCORDRPC
#include "pc/discord/discordrpc.h"
#endif
#include "pc/network/version.h"
#include "pc/network/network_player.h"
#include "pc/djui/djui.h"

#include "pc/mod_list.h"

OSMesg D_80339BEC;
OSMesgQueue gSIEventMesgQueue;

s8 gResetTimer;
s8 D_8032C648;
s8 gDebugLevelSelect;
s8 gShowProfiler;
s8 gShowDebugText;

s32 gRumblePakPfs;
struct RumbleData gRumbleDataQueue[3];
struct StructSH8031D9B0 gCurrRumbleSettings;

static struct AudioAPI *audio_api;
struct GfxWindowManagerAPI *wm_api;
static struct GfxRenderingAPI *rendering_api;

extern void gfx_run(Gfx *commands);
extern void thread5_game_loop(void *arg);
extern void create_next_audio_buffer(s16 *samples, u32 num_samples);
void game_loop_one_iteration(void);

void dispatch_audio_sptask(UNUSED struct SPTask *spTask) {
}

void set_vblank_handler(UNUSED s32 index, UNUSED struct VblankHandler *handler, UNUSED OSMesgQueue *queue, UNUSED OSMesg *msg) {
}

static bool inited = false;

#include "game/display.h" // for gGlobalTimer
void send_display_list(struct SPTask *spTask) {
    if (!inited) return;
    gfx_run((Gfx *)spTask->task.t.data_ptr);
}

#ifdef VERSION_EU
#define SAMPLES_HIGH 656
#define SAMPLES_LOW 640
#else
#define SAMPLES_HIGH 544
#define SAMPLES_LOW 528
#endif

static inline void patch_interpolations(void) {
    extern void mtx_patch_interpolated(void);
    extern void patch_screen_transition_interpolated(void);
    extern void patch_title_screen_scales(void);
    extern void patch_interpolated_dialog(void);
    extern void patch_interpolated_hud(void);
    extern void patch_interpolated_paintings(void);
    extern void patch_interpolated_bubble_particles(void);
    extern void patch_interpolated_snow_particles(void);
    extern void djui_render_patch(void);
    mtx_patch_interpolated();
    patch_screen_transition_interpolated();
    patch_title_screen_scales();
    patch_interpolated_dialog();
    patch_interpolated_hud();
    patch_interpolated_paintings();
    patch_interpolated_bubble_particles();
    patch_interpolated_snow_particles();
    djui_render_patch();
}

void produce_one_frame(void) {
    network_update();
    gfx_start_frame();

    const f32 master_mod = (f32)configMasterVolume / 127.0f;
    set_sequence_player_volume(SEQ_PLAYER_LEVEL, (f32)configMusicVolume / 127.0f * master_mod);
    set_sequence_player_volume(SEQ_PLAYER_SFX, (f32)configSfxVolume / 127.0f * master_mod);
    set_sequence_player_volume(SEQ_PLAYER_ENV, (f32)configEnvVolume / 127.0f * master_mod);

    game_loop_one_iteration();
    smlua_update();
    thread6_rumble_loop(NULL);

    int samples_left = audio_api->buffered();
    u32 num_audio_samples = samples_left < audio_api->get_desired_buffered() ? SAMPLES_HIGH : SAMPLES_LOW;
    //printf("Audio samples: %d %u\n", samples_left, num_audio_samples);
    s16 audio_buffer[SAMPLES_HIGH * 2 * 2];
    for (int i = 0; i < 2; i++) {
        /*if (audio_cnt-- == 0) {
            audio_cnt = 2;
        }
        u32 num_audio_samples = audio_cnt < 2 ? 528 : 544;*/
        create_next_audio_buffer(audio_buffer + i * (num_audio_samples * 2), num_audio_samples);
    }
    //printf("Audio samples before submitting: %d\n", audio_api->buffered());

    audio_api->play((u8 *)audio_buffer, 2 * num_audio_samples * 4);

    gfx_end_frame();

    if (config60Fps) {
        gfx_start_frame();
        patch_interpolations();
        send_display_list(gGfxSPTask);
        gfx_end_frame();
    }
}

void audio_shutdown(void) {
    if (audio_api) {
        if (audio_api->shutdown) audio_api->shutdown();
        audio_api = NULL;
    }
}

void game_deinit(void) {
#ifdef DISCORDRPC
    discord_shutdown();
#endif
    configfile_save(configfile_name());
    controller_shutdown();
    audio_shutdown();
    gfx_shutdown();
    network_shutdown(true);
    smlua_shutdown();
    mod_list_shutdown();
    inited = false;
}

void game_exit(void) {
    game_deinit();
#ifndef TARGET_WEB
    exit(0);
#endif
}

#ifdef TARGET_WEB
static void em_main_loop(void) {
}

static void request_anim_frame(void (*func)(double time)) {
    EM_ASM(requestAnimationFrame(function(time) {
        dynCall("vd", $0, [time]);
    }), func);
}

static void on_anim_frame(double time) {
    static double target_time;

    time *= 0.03; // milliseconds to frame count (33.333 ms -> 1)

    if (time >= target_time + 10.0) {
        // We are lagging 10 frames behind, probably due to coming back after inactivity,
        // so reset, with a small margin to avoid potential jitter later.
        target_time = time - 0.010;
    }

    for (int i = 0; i < 2; i++) {
        // If refresh rate is 15 Hz or something we might need to generate two frames
        if (time >= target_time) {
            produce_one_frame();
            target_time = target_time + 1.0;
        }
    }

    if (inited) // only continue if the init flag is still set
        request_anim_frame(on_anim_frame);
}
#endif

void main_func(void) {
    const char *gamedir = gCLIOpts.GameDir[0] ? gCLIOpts.GameDir : FS_BASEDIR;
    const char *userpath = gCLIOpts.SavePath[0] ? gCLIOpts.SavePath : sys_user_path();
    fs_init(sys_ropaths, gamedir, userpath);

    mod_list_init();
    configfile_load(configfile_name());
    if (configPlayerModel >= CT_MAX) { configPlayerModel = 0; }
    if (configPlayerPalette >= 24) { configPlayerPalette = 0; }

    if (gCLIOpts.FullScreen == 1)
        configWindow.fullscreen = true;
    else if (gCLIOpts.FullScreen == 2)
        configWindow.fullscreen = false;

    const size_t poolsize = gCLIOpts.PoolSize ? gCLIOpts.PoolSize : DEFAULT_POOL_SIZE;
    u64 *pool = malloc(poolsize);
    if (!pool) sys_fatal("Could not alloc %u bytes for main pool.\n", poolsize);
    main_pool_init(pool, pool + poolsize / sizeof(pool[0]));
    gEffectsMemoryPool = mem_pool_init(0x4000, MEMORY_POOL_LEFT);

    #if defined(WAPI_SDL1) || defined(WAPI_SDL2)
    wm_api = &gfx_sdl;
    #elif defined(WAPI_DXGI)
    wm_api = &gfx_dxgi;
    #elif defined(WAPI_DUMMY)
    wm_api = &gfx_dummy_wm_api;
    #else
    #error No window API!
    #endif

    #if defined(RAPI_D3D11)
    rendering_api = &gfx_direct3d11_api;
    # define RAPI_NAME "DirectX 11"
    #elif defined(RAPI_D3D12)
    rendering_api = &gfx_direct3d12_api;
    # define RAPI_NAME "DirectX 12"
    #elif defined(RAPI_GL) || defined(RAPI_GL_LEGACY)
    rendering_api = &gfx_opengl_api;
    # ifdef USE_GLES
    #  define RAPI_NAME "OpenGL ES"
    # else
    #  define RAPI_NAME "OpenGL"
    # endif
    #elif defined(RAPI_DUMMY)
    rendering_api = &gfx_dummy_renderer_api;
    #else
    #error No rendering API!
    #endif

    char* version = get_version();
    char window_title[96] = { 0 };
#ifdef GIT_HASH
    snprintf(window_title, 96, "sm64ex-coop: %s [%s]", version, GIT_HASH);
#else
    snprintf(window_title, 96, "sm64ex-coop: %s", version);
#endif

    gfx_init(wm_api, rendering_api, window_title);
    wm_api->set_keyboard_callbacks(keyboard_on_key_down, keyboard_on_key_up, keyboard_on_all_keys_up, keyboard_on_text_input);

    #if defined(AAPI_SDL1) || defined(AAPI_SDL2)
    if (audio_api == NULL && audio_sdl.init()) 
        audio_api = &audio_sdl;
    #endif

    if (audio_api == NULL) {
        audio_api = &audio_null;
    }

    djui_init();

#ifdef UNSTABLE_BRANCH
    djui_popup_create("This is an \\#ffa0a0\\unstable\\#dcdcdc\\ branch build.\nExpect many strange bugs.", 2);
#endif

    if (gCLIOpts.Network == NT_CLIENT) {
        network_set_system(NS_SOCKET);
        strncpy(configJoinIp, gCLIOpts.JoinIp, IP_MAX_LEN);
        configJoinPort = gCLIOpts.NetworkPort;
        network_init(NT_CLIENT);
    } else if (gCLIOpts.Network == NT_SERVER) {
        network_set_system(NS_SOCKET);
        configHostPort = gCLIOpts.NetworkPort;
        network_init(NT_SERVER);
        djui_panel_shutdown();
        djui_panel_modlist_create(NULL);
    } else {
        network_init(NT_NONE);
    }

    audio_init();
    sound_init();
    network_player_init();

    thread5_game_loop(NULL);

    inited = true;

#ifdef EXTERNAL_DATA
    // precache data if needed
    if (configPrecacheRes) {
        fprintf(stdout, "precaching data\n");
        fflush(stdout);
        gfx_precache_textures();
    }
#endif

#ifdef DISCORDRPC
    discord_init();
#endif

#ifdef TARGET_WEB
    emscripten_set_main_loop(em_main_loop, 0, 0);
    request_anim_frame(on_anim_frame);
#else
    while (true) {
        wm_api->main_loop(produce_one_frame);
#ifdef DISCORDRPC
        discord_update_rich_presence();
#endif
#ifdef DEBUG
        fflush(stdout);
        fflush(stderr);
#endif
    }
#endif
}

int main(int argc, char *argv[]) {
    parse_cli_opts(argc, argv);
    main_func();
    return 0;
}
