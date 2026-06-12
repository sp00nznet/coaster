/*
 * main.c - Coaster Recompilation entry point
 *
 * Boots the recompiled game: allocates the flat 8086 address space, brings up
 * the DOS compatibility layer and the SDL2 platform, loads the player's own
 * COASTER.EXE (unpacked on the fly by startup.c), and runs it.
 *
 * Part of the Coaster Recomp project (sp00nznet/coaster)
 */

#include "recomp/cpu.h"
#include "recomp/dos_compat.h"
#include "platform/sdl_platform.h"
#include "coaster_recomp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Provided by startup.c */
int  coaster_load(CPU *cpu, const char *exe_path);
void coaster_start(CPU *cpu);

/* Pumps SDL events and paints the framebuffer; the game calls this through the
 * DOS layer whenever it blocks for input or a timer tick. */
static void game_poll_callback(void *platform_ctx, void *dos_state, const void *cpu)
{
    Platform *plat = (Platform *)platform_ctx;
    DosState *dos = (DosState *)dos_state;
    const CPU *c = (const CPU *)cpu;
    platform_poll_events(plat, dos);
    platform_render(plat, c, dos);
}

static void usage(const char *prog)
{
    printf("Coaster - static recompilation of Roller Coaster Construction Set (1993)\n\n");
    printf("usage: %s [COASTER.EXE] [--gamedir DIR] [--scale N]\n\n", prog);
    printf("  COASTER.EXE   path to your original packed game executable\n");
    printf("                (default: ./COASTER.EXE)\n");
    printf("  --gamedir DIR folder holding COASTER1.RSC and the .TRA tracks\n");
    printf("                (default: the directory of COASTER.EXE)\n");
    printf("  --scale N     integer window scale (default 3 -> 960x600)\n");
}

int main(int argc, char *argv[])
{
    const char *exe_path = "COASTER.EXE";
    const char *game_dir = NULL;
    int scale = WINDOW_SCALE;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--gamedir") && i + 1 < argc) {
            game_dir = argv[++i];
        } else {
            exe_path = argv[i];
        }
    }

    /* Default the game dir to wherever the EXE lives. */
    char dirbuf[260];
    if (!game_dir) {
        strncpy(dirbuf, exe_path, sizeof dirbuf - 1);
        dirbuf[sizeof dirbuf - 1] = 0;
        char *slash = strrchr(dirbuf, '/');
        char *bslash = strrchr(dirbuf, '\\');
        if (bslash > slash) slash = bslash;
        if (slash) { *slash = 0; game_dir = dirbuf; }
        else game_dir = ".";
    }

    printf("[MAIN] Coaster recomp\n");
    printf("[MAIN] EXE:      %s\n", exe_path);
    printf("[MAIN] Game dir: %s\n", game_dir);

    CPU cpu;
    cpu_init(&cpu);
    if (cpu_alloc_mem(&cpu) < 0) {
        fprintf(stderr, "[MAIN] out of memory allocating address space\n");
        return 1;
    }

    static DosState dos;
    dos_init(&dos, &cpu, game_dir);

    Platform plat;
    if (platform_init(&plat, scale) < 0) {
        fprintf(stderr, "[MAIN] SDL platform init failed\n");
        cpu_free(&cpu);
        return 1;
    }
    dos.poll_events = game_poll_callback;
    dos.platform_ctx = &plat;

    if (coaster_load(&cpu, exe_path) < 0) {
        fprintf(stderr, "[MAIN] failed to load game image\n");
        platform_shutdown(&plat);
        cpu_free(&cpu);
        return 1;
    }

    /* Hand control to the recompiled game. Returns when it exits (INT 21h/4Ch
     * sets cpu.halted) or the window is closed. */
    coaster_start(&cpu);

    /* Keep the window up after the game returns so the last frame is visible. */
    while (plat.running && !cpu.halted) {
        platform_poll_events(&plat, &dos);
        platform_render(&plat, &cpu, &dos);
        platform_delay(16);
    }

    platform_shutdown(&plat);
    cpu_free(&cpu);
    return 0;
}
