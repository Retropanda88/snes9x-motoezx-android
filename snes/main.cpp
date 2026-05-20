#include <SDL/SDL.h>
#include <stdio.h>
#include <stdlib.h>

// Declaración formal de la función puente ubicada en snes_player.cpp
extern void run_snes_emulator(const char *rom_filename);

int main(int argc, char *argv[])
{
    const char *rom_to_load = "game.smc"; // Nombre de la ROM por defecto en la carpeta

    printf("[SISTEMA] Inicializando cargador del emulador de SNES...\n");

    // 1. Verificar si el usuario pasó una ROM por argumentos en la terminal de C4droid
    if (argc > 1) 
    {
        rom_to_load = argv[1];
        printf("[INFO] Cargando ROM especificada por parámetro: %s\n", rom_to_load);
    } 
    else 
    {
        printf("[INFO] No se especificó ninguna ROM. Buscando archivo por defecto: %s\n", rom_to_load);
    }

    // 2. Ejecutar de forma directa el bucle del emulador de Super Nintendo
    // Esta llamada no regresará hasta que presiones la tecla ESCAPE en el juego
    run_snes_emulator(rom_to_load);

    printf("[SISTEMA] Emulador cerrado correctamente. ¡Hasta luego!\n");
    
    return 0;
}
