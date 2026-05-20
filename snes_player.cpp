#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <SDL/SDL.h>

// =========================================================
// SNES9X
// =========================================================

#include "snes/snes9x.h"
#include "snes/apu.h"
#include "snes/memmap.h"
#include "snes/soundux.h"
#include "snes/gfx.h"
#include "snes/display.h"
#include "snes/cpuexec.h"

// =========================================================
// CONFIG
// =========================================================

#define SNES_SAMPLE_RATE    22050
#define SNES_FPS            60.0988
#define AUDIO_SAMPLES       512
#define RING_BUF_SIZE       65536

// =========================================================
// GLOBALS
// =========================================================

static bool GameLooping = true;
static uint32 snes_joypad = 0;

int m_nVolume = 100;

// =========================================================
// AUDIO BUFFER
// =========================================================

static uint8_t RingBuffer[RING_BUF_SIZE];

static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;

// =========================================================
// EXTERN MIXBUFFER
// =========================================================

extern "C"
{
    extern uint8_t MixBuffer[];
}

// =========================================================
// AUDIO BUFFER UTILS
// =========================================================

static inline uint32_t audio_buffer_used()
{
    uint32_t head = ring_head;
    uint32_t tail = ring_tail;

    if (head >= tail)
        return head - tail;

    return RING_BUF_SIZE - tail + head;
}

static inline uint32_t audio_buffer_free()
{
    return (RING_BUF_SIZE - 1) - audio_buffer_used();
}

// =========================================================
// SDL AUDIO CALLBACK
// =========================================================

void snes_audio_callback(
    void *userdata,
    Uint8 *stream,
    int len)
{
    uint32_t available =
        audio_buffer_used();

    if (available < (uint32_t)len)
    {
        memset(stream, 0, len);
        return;
    }

    for (int i = 0; i < len; i++)
    {
        stream[i] =
            RingBuffer[ring_tail];

        ring_tail =
            (ring_tail + 1) % RING_BUF_SIZE;
    }
}

// =========================================================
// OPEN SOUND
// =========================================================

bool8_32 S9xOpenSoundDevice(
    int mode,
    bool8_32 stereo,
    int buffer_size)
{
    memset(RingBuffer, 0, sizeof(RingBuffer));

    ring_head = 0;
    ring_tail = 0;

    SDL_AudioSpec wanted;

    wanted.freq = SNES_SAMPLE_RATE;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = AUDIO_SAMPLES;
    wanted.callback = snes_audio_callback;
    wanted.userdata = NULL;

    if (SDL_OpenAudio(&wanted, NULL) < 0)
    {
        printf("SDL_OpenAudio failed\n");
        return FALSE;
    }

    so.sound_fd = 1;
    so.playback_rate = SNES_SAMPLE_RATE;
    so.stereo = TRUE;
    so.sixteen_bit = TRUE;
    so.buffer_size = buffer_size;
    so.samples_mixed_so_far = 0;
    so.play_position = 0;
    so.mute_sound = FALSE;

    SDL_PauseAudio(0);

    return TRUE;
}

// =========================================================
// PROCESS SOUND
// =========================================================

void S9xProcessSound()
{
    static double sample_accumulator = 0.0;

    sample_accumulator +=
        ((double)SNES_SAMPLE_RATE / SNES_FPS);

    int sample_count =
        (int)sample_accumulator;

    sample_accumulator -= sample_count;

    if (sample_count <= 0)
        return;

    // =====================================================
    // IMPORTANTE:
    // Algunas versiones de Snes9x esperan
    // sample_count * 2 en stereo
    // =====================================================

    S9xMixSamplesO(
        MixBuffer,
        sample_count * 2,
        0);

    // 16-bit stereo
    int bytes_ready =
        sample_count * 4;

    if (audio_buffer_free() <
        (uint32_t)bytes_ready)
    {
        return;
    }

    uint8_t *src =
        (uint8_t *)MixBuffer;

    SDL_LockAudio();

    for (int i = 0; i < bytes_ready; i++)
    {
        RingBuffer[ring_head] = src[i];

        ring_head =
            (ring_head + 1) %
            RING_BUF_SIZE;
    }

    SDL_UnlockAudio();

    so.samples_mixed_so_far = 0;
}

// =========================================================
// AUDIO DRIVEN SYNC
// =========================================================

void S9xSyncSpeed()
{
    while (audio_buffer_used() >
          (RING_BUF_SIZE / 2))
    {
        SDL_Delay(1);
    }
}

// =========================================================
// INPUT
// =========================================================

void do_snes_keypad()
{
    snes_joypad = 0x80000000;

    Uint8 *keystate =
        SDL_GetKeyState(NULL);

    if (keystate[SDLK_UP])
        snes_joypad |= SNES_UP_MASK;

    if (keystate[SDLK_DOWN])
        snes_joypad |= SNES_DOWN_MASK;

    if (keystate[SDLK_LEFT])
        snes_joypad |= SNES_LEFT_MASK;

    if (keystate[SDLK_RIGHT])
        snes_joypad |= SNES_RIGHT_MASK;

    if (keystate[SDLK_b])
        snes_joypad |= SNES_A_MASK;

    if (keystate[SDLK_d])
        snes_joypad |= SNES_B_MASK;

    if (keystate[SDLK_a])
        snes_joypad |= SNES_X_MASK;

    if (keystate[SDLK_c])
        snes_joypad |= SNES_Y_MASK;

    if (keystate[SDLK_RETURN])
        snes_joypad |= SNES_START_MASK;

    if (keystate[SDLK_SPACE])
        snes_joypad |= SNES_SELECT_MASK;

    if (keystate[SDLK_q])
        snes_joypad |= SNES_TL_MASK;

    if (keystate[SDLK_w])
        snes_joypad |= SNES_TR_MASK;

    if (keystate[SDLK_ESCAPE])
        GameLooping = false;
}

// =========================================================
// JOYPAD CALLBACK
// =========================================================

uint32 S9xReadJoypad(int port)
{
    if (port == 0)
        return snes_joypad;

    return 0x80000000;
}

// =========================================================
// MAIN
// =========================================================

extern "C"
void run_snes_emulator(const char *fn)
{
    ZeroMemory(&Settings, sizeof(Settings));

    // =====================================================
    // SETTINGS
    // =====================================================

    Settings.SoundPlaybackRate =
        SNES_SAMPLE_RATE;

    Settings.Stereo = TRUE;

    Settings.SoundBufferSize =
        AUDIO_SAMPLES;

    Settings.CyclesPercentage = 100;

    Settings.DisableSoundEcho = FALSE;

    Settings.APUEnabled = TRUE;
    Settings.NextAPUEnabled = TRUE;

    Settings.H_Max =
        SNES_CYCLES_PER_SCANLINE;

    Settings.ShutdownMaster = TRUE;

    Settings.FrameTimePAL = 20000;

    Settings.FrameTimeNTSC = 16667;

    Settings.FrameTime =
        Settings.FrameTimeNTSC;

    Settings.DisableSampleCaching =
        FALSE;

    Settings.DisableMasterVolume =
        FALSE;

    Settings.Transparency = TRUE;

    Settings.SixteenBit = TRUE;

    Settings.SupportHiRes = FALSE;

    Settings.HBlankStart =
        (256 * Settings.H_Max) /
        SNES_HCOUNTER_MAX;

    // =====================================================
    // VOLUME
    // =====================================================

    SoundData.master_volume_left = 127;
    SoundData.master_volume_right = 127;

    // =====================================================
    // INIT CORE
    // =====================================================

    if (!Memory.Init())
        return;

    if (!S9xInitAPU())
        return;

    // =====================================================
    // GFX
    // =====================================================

    GFX.Screen =
        (uint8 *)malloc(320 * 240 * 2);

    GFX.Pitch = 320 * 2;

    GFX.SubScreen =
        (uint8 *)malloc(512 * 480 * 2);

    GFX.ZBuffer =
        (uint8 *)malloc(512 * 480);

    GFX.SubZBuffer =
        (uint8 *)malloc(512 * 480);

    if (!S9xGraphicsInit())
        return;

    // =====================================================
    // LOAD ROM
    // =====================================================

    if (!Memory.LoadROM(fn))
    {
        printf("ROM load failed\n");
        return;
    }

    // =====================================================
    // AUDIO
    // =====================================================

    if (!S9xOpenSoundDevice(
            Settings.SoundPlaybackRate,
            Settings.Stereo,
            Settings.SoundBufferSize))
    {
        return;
    }

    S9xSetPlaybackRate(
        SNES_SAMPLE_RATE);

    S9xResetSound(FALSE);

    S9xSetSoundControl(0xFF);

    S9xSetSoundMute(FALSE);

    // =====================================================
    // VIDEO
    // =====================================================

    S9xSetRenderPixelFormat(RGB565);

    SDL_Surface *screen =
        SDL_SetVideoMode(
            320,
            240,
            16,
            SDL_DOUBLEBUF);

    if (!screen)
    {
        printf("SDL_SetVideoMode failed\n");
        return;
    }

    // =====================================================
    // MAIN LOOP
    // =====================================================

    GameLooping = true;

    SDL_Event event;

    while (GameLooping)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                GameLooping = false;
            }
        }

        // INPUT
        do_snes_keypad();

        // EMULATE FRAME
        S9xMainLoop();

        // AUDIO
        S9xProcessSound();

        // SYNC
        S9xSyncSpeed();

        // VIDEO
        if (SDL_MUSTLOCK(screen))
            SDL_LockSurface(screen);

        memset(
            screen->pixels,
            0,
            screen->pitch * 240);

        uint16_t *src =
            (uint16_t *)GFX.Screen;

        uint16_t *dst =
            (uint16_t *)screen->pixels;

        int pitch =
            screen->pitch / 2;

        int margin_x = 32;
        int margin_y = 8;

        for (int y = 0; y < 224; y++)
        {
            uint16_t *src_row =
                src + (y * 320);

            uint16_t *dst_row =
                dst +
                ((y + margin_y) * pitch) +
                margin_x;

            memcpy(
                dst_row,
                src_row,
                256 * sizeof(uint16_t));
        }

        if (SDL_MUSTLOCK(screen))
            SDL_UnlockSurface(screen);

        SDL_Flip(screen);
    }

    // =====================================================
    // CLEANUP
    // =====================================================

    SDL_CloseAudio();

    Memory.Deinit();

    S9xDeinitAPU();

    S9xGraphicsDeinit();

    if (GFX.Screen)
    {
        free(GFX.Screen);
        GFX.Screen = NULL;
    }

    if (GFX.SubScreen)
    {
        free(GFX.SubScreen);
        GFX.SubScreen = NULL;
    }

    if (GFX.ZBuffer)
    {
        free(GFX.ZBuffer);
        GFX.ZBuffer = NULL;
    }

    if (GFX.SubZBuffer)
    {
        free(GFX.SubZBuffer);
        GFX.SubZBuffer = NULL;
    }
}

// =========================================================
// STUBS
// =========================================================

void S9xExit()
{
    GameLooping = false;
}

bool8 S9xReadMousePosition(
    int which1,
    int *x,
    int *y,
    uint32 *buttons)
{
    return FALSE;
}

bool8 S9xReadSuperScopePosition(
    int *x,
    int *y,
    uint32 *buttons)
{
    return FALSE;
}

void S9xAutoSaveSRAM()
{
}

extern "C"
{
    void S9xMessage(
        int type,
        int number,
        const char *message)
    {
    }

    void S9xGenerateSound()
    {
    }

    void S9xPutImage(
        int width,
        int height)
    {
    }

    bool8_32 S9xDeinitUpdate(
        int width,
        int height)
    {
        return TRUE;
    }

    void S9xSetPalette()
    {
    }
}