#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <SDL/SDL.h>

// Cabeceras del núcleo del emulador
#include "snes/snes9x.h"
#include "snes/apu.h"
#include "snes/memmap.h"
#include "snes/soundux.h"
#include "snes/gfx.h"
#include "snes/display.h"
#include "snes/cpuexec.h"

// Constantes de rendimiento y sincronización
#define SNES_SAMPLE_RATE    44100
#define SNES_SOUND_BUF_LEN  1024
#define RING_BUF_SIZE       (SNES_SOUND_BUF_LEN * 16)
#define TARGET_FPS          100
#define DELAY_FRAME         (1000 / TARGET_FPS)

// Búfer circular para el streaming de audio en SDL
static uint8_t RingBuffer[RING_BUF_SIZE];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;

static uint32_t snes_joypad = 0;
static bool GameLooping = true;
int m_nVolume = 100;

extern "C"
{
	extern uint8_t MixBuffer[];
}

// Callback de audio de SDL para alimentar las bocinas
void snes_audio_callback(void *userdata, uint8_t * stream, int len)
{
	uint32_t head = ring_head;
	uint32_t tail = ring_tail;
	uint32_t available = (head >= tail) ? (head - tail) : (RING_BUF_SIZE - tail + head);

	if (available < (uint32_t) len)
	{
		memset(stream, 0, len);
		return;
	}

	for (int i = 0; i < len; i++)
	{
		stream[i] = RingBuffer[ring_tail];
		ring_tail = (ring_tail + 1) % RING_BUF_SIZE;
	}
}

// Abre el hardware de audio usando la librería SDL
bool8_32 S9xOpenSoundDevice(int mode, bool8_32 stereo, int buffer_size)
{
	memset(RingBuffer, 0, sizeof(RingBuffer));
	ring_head = 0;
	ring_tail = 0;

	SDL_AudioSpec wanted;
	wanted.freq = 22050;         
	wanted.format = AUDIO_S16SYS;
	wanted.channels = 2;
	wanted.samples = 512;
	wanted.callback = snes_audio_callback;
	wanted.userdata = NULL;

	if (SDL_OpenAudio(&wanted, NULL) < 0)
		return FALSE;

	so.sound_fd = 7;
	so.playback_rate = 22050;
	so.stereo = 1;
	so.sixteen_bit = 1;
	so.buffer_size = buffer_size;
	so.samples_mixed_so_far = 0;
	so.play_position = 0;
	so.mute_sound = FALSE;

	SDL_PauseAudio(0);
	return TRUE;
}

// Copia las muestras generadas por la APU de SNES al búfer de SDL
void S9xProcessSound()
{
	// EXTRACTOR MANUAL: Forzamos al mezclador clásico a vaciar la APU en MixBuffer.
	// 44100Hz / 60 FPS = 735 muestras estéreo por fotograma.
	//int sample_count = 367; 
	//int sample_count = 735; 
	int sample_count = 735; 
	
	// Llamamos a la función nativa que sí enlaza para obligar al núcleo a mezclar
	S9xMixSamplesO(MixBuffer, sample_count, 0);

	// 1 muestra estéreo de 16 bits ocupará exactamente 4 bytes
	int bytes_ready = sample_count * 2; // 735 * 4 = 2940 bytes
	
	uint32_t head = ring_head;
	uint32_t tail = ring_tail;
	uint32_t ocupado = (head >= tail) ? (head - tail) : (RING_BUF_SIZE - tail + head);
	uint32_t espacio_libre = RING_BUF_SIZE - ocupado - 1;

	if (espacio_libre < (uint32_t) bytes_ready)
	{
		so.samples_mixed_so_far = 0;
		return;
	}

	SDL_LockAudio();
	uint8_t *core_sound_ptr = (uint8_t *) MixBuffer;
	for (int i = 0; i < bytes_ready; i++)
	{
		RingBuffer[ring_head] = core_sound_ptr[i];
		ring_head = (ring_head + 1) % RING_BUF_SIZE;
	}
	SDL_UnlockAudio();

	so.samples_mixed_so_far = 0;
}


void S9xSyncSpeed() 
{
    static uint32_t next_frame_time = 0;
    uint32_t now = SDL_GetTicks();
    if (next_frame_time == 0) next_frame_time = now;

    if (now < next_frame_time) {
        SDL_Delay(next_frame_time - now);
    }
    next_frame_time += DELAY_FRAME;

    // Forzamos el volcado directo del audio procesado hacia SDL
    S9xProcessSound();
}

// Lee el estado de los controles
void do_snes_keypad()
{
	snes_joypad = 0x80000000;
	Uint8 *keystate = SDL_GetKeyState(NULL);

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

uint32 S9xReadJoypad(int port)
{
	if (port == 0)
		return snes_joypad;
	return 0x80000000;
}

// =========================================================
// FUNCIÓN DE ENTRADA LLAMADA DESDE MAIN.CPP
// =========================================================
extern "C" void run_snes_emulator(const char *fn)
{
	ZeroMemory(&Settings, sizeof(Settings));

	Settings.SoundPlaybackRate = 4;	// 22050Hz
	Settings.Stereo = TRUE;
	Settings.SoundBufferSize = 1024;
	Settings.CyclesPercentage = 100;
	Settings.DisableSoundEcho = FALSE;
	Settings.APUEnabled = Settings.NextAPUEnabled = TRUE;
	Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
	Settings.ShutdownMaster = TRUE;
	Settings.FrameTimePAL = 22000;
	Settings.FrameTimeNTSC = 16667;
	Settings.FrameTime = Settings.FrameTimeNTSC;
	Settings.DisableSampleCaching = FALSE;
	Settings.DisableMasterVolume = FALSE; 
	Settings.Transparency = TRUE;
	Settings.SixteenBit = TRUE;
	Settings.SupportHiRes = FALSE;
	
	SoundData.master_volume_left = 127;
	SoundData.master_volume_right = 127;
	SoundData.master_volume[0] = 127;
	SoundData.master_volume[1] = 127;

	Settings.NextAPUEnabled = TRUE;
	Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;

	if (!Memory.Init() || !S9xInitAPU())
		return;

	GFX.Screen = (uint8 *) malloc(320 * 240 * 2);
	GFX.Pitch = 320 * 2;
	GFX.SubScreen = (uint8 *) malloc(512 * 480 * 2);
	GFX.ZBuffer = (uint8 *) malloc(512 * 480 * 2);
	GFX.SubZBuffer = (uint8 *) malloc(512 * 480 * 2);

	if (!S9xGraphicsInit())
		return;

	// 1. CARGAMOS LA ROM
	if (!Memory.LoadROM(fn))
		return;

	// 2. DISPARADORES DE AUDIO TRAS LA RAM LIMPIA
	S9xOpenSoundDevice(Settings.SoundPlaybackRate, Settings.Stereo, Settings.SoundBufferSize);
	S9xSetPlaybackRate(22050); 
	S9xResetSound(FALSE);      // Reseteo forzado del procesador de audio clasico
	S9xSetSoundControl(0xFF);  // Habilitar todos los canales
	S9xSetSoundMute(FALSE);    // Desactivar mute por hardware
	S9xSetRenderPixelFormat(RGB565);

	SDL_Surface *screen = SDL_SetVideoMode(320, 240, 16, SDL_HWSURFACE | SDL_DOUBLEBUF);
	if (!screen)
		return;

	GameLooping = true;
	SDL_Event event;

	while (GameLooping)
	{
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
				GameLooping = false;
		}

		do_snes_keypad();
		
		// Corre un fotograma completo de emulación
		S9xMainLoop();

		// Regula FPS y extrae el audio mezclado hacia SDL
		S9xSyncSpeed();

		SDL_LockSurface(screen);

		uint16_t *snes_buffer = (uint16_t *) GFX.Screen;
		uint16_t *sdl_pixels = (uint16_t *) screen->pixels;
		int sdl_pitch = screen->pitch / 2;	

		const int snes_w = 256;
		const int snes_h = 224;

		for (int y = 0; y < 240; y++)
		{
			int snes_y = (y * snes_h) / 240;
			uint16_t *src_row = snes_buffer + (snes_y * 320);	
			uint16_t *dest_row = sdl_pixels + (y * sdl_pitch);

			for (int x = 0; x < 320; x++)
			{
				int snes_x = (x * snes_w) / 320;
				dest_row[x] = src_row[snes_x];
			}
		}

		SDL_UnlockSurface(screen);
		SDL_Flip(screen);
	}

	SDL_CloseAudio();
	Memory.Deinit();
	S9xDeinitAPU();
	S9xGraphicsDeinit();

	if (GFX.Screen) { free(GFX.Screen); GFX.Screen = NULL; }
	if (GFX.SubScreen) { free(GFX.SubScreen); GFX.SubScreen = NULL; }
	if (GFX.ZBuffer) { free(GFX.ZBuffer); GFX.ZBuffer = NULL; }
	if (GFX.SubZBuffer) { free(GFX.SubZBuffer); GFX.SubZBuffer = NULL; }
}

void S9xExit()
{
	GameLooping = false;
}

bool8 S9xReadMousePosition(int which1, int *x, int *y, uint32 * buttons)
{
	return FALSE;
}

bool8 S9xReadSuperScopePosition(int *x, int *y, uint32 * buttons)
{
	return FALSE;
}

void S9xAutoSaveSRAM()
{
}

extern "C"
{
	void S9xMessage(int type, int number, const char *message)
	{
	}

	void S9xGenerateSound()
	{
		// Satisface cpuexec.cpp sin causar errores de enlazador
	}

	void S9xPutImage(int width, int height)
	{
		// Satisface display.h
	}

	bool8_32 S9xDeinitUpdate(int width, int height)
	{
		return TRUE;
	}

	void S9xSetPalette()
	{
	}

	bool8 S9xOpenSnapshotFile(const char *filepath, bool8 read_only, STREAM * stream)
	{
		if (read_only)
			*stream = OPEN_STREAM(filepath, "rb");
		else
			*stream = OPEN_STREAM(filepath, "wb");
		return (*stream != NULL);
	}

	void S9xCloseSnapshotFile(STREAM stream)
	{
		CLOSE_STREAM(stream);
	}
}
