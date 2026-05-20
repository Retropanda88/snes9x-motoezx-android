#include <SDL/SDL.h>
#include <stdio.h>
#include <stdlib.h>

// Forzamos el enlace tipo C para evitar que C++ altere el nombre de la
// función (Name Mangling)
extern "C" void run_snes_emulator(const char *rom_filename);

int main(int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
	{
		// ...
	}

	const char *rom_to_load = "game3.smc";	// Nombre de la ROM por defecto

	printf("[SISTEMA] Inicializando cargador del emulador de SNES...\n");

	// Verificar si el usuario pasó una ROM por argumentos
	if (argc > 1)
	{
		rom_to_load = argv[1];
		printf("[INFO] Cargando ROM especificada por parámetro: %s\n", rom_to_load);
	}
	else
	{
		printf("[INFO] No se especificó ninguna ROM. Buscando archivo por defecto: %s\n",
			   rom_to_load);
	}

	// Arrancamos el emulador
	run_snes_emulator(rom_to_load);

	printf("[SISTEMA] Emulador cerrado correctamente. ¡Hasta luego!\n");

	return 0;
}
