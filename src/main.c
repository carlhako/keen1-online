/* Commander Keen 1: Marooned on Mars - native Linux port.
 *
 * Reconstructed from KEEN1.EXE v1.1 (reverse engineered with REA + Ghidra).
 * Needs the original data files (EGAHEAD.CK1, LEVELnn.CK1, KEEN1.EXE, ...). */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "game.h"
#include "gfx.h"
#include "kdata.h"
#include "platform.h"

extern void palettes_init(void);

static int has_game(const char *dir)
{
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/EGAHEAD.CK1", dir);
    if (access(path, R_OK) == 0)
        return 1;
    snprintf(path, sizeof path, "%s/egahead.ck1", dir);
    return access(path, R_OK) == 0;
}

static void find_data_dir(const char *opt)
{
    if (opt) {
        data_set_dir(opt);
        return;
    }
    const char *env = getenv("KEEN1_DATA");
    if (env && has_game(env)) {
        data_set_dir(env);
        return;
    }
    if (has_game(".")) {
        data_set_dir(".");
        return;
    }
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = 0;
        char *slash = strrchr(exe, '/');
        if (slash)
            *slash = 0;
        char path[PATH_MAX + 8];
        snprintf(path, sizeof path, "%s/data", exe);
        if (has_game(path)) {
            data_set_dir(path);
            return;
        }
        if (has_game(exe)) {
            data_set_dir(exe);
            return;
        }
    }
    data_set_dir(".");
}

static void usage(void)
{
    puts("Usage: keen1 [--data DIR] [--fullscreen] [--scale N] [--level N]\n"
         "  --data DIR    directory with the Keen 1 files (KEEN1.EXE, *.CK1)\n"
         "  --fullscreen  start in fullscreen (Alt+Enter toggles)\n"
         "  --scale N     window size multiplier (default 3)\n"
         "  --level N     play level N directly (like KEEN1 /LEVELnn)\n"
         "  --seed N      random seed (default: from the clock)\n"
         "  --local2      two co-op players in one window; player 2 uses Q W E / A D / Z S C, V jump, B pogo\n"
         "  --coop        co-op rules with one player (a partner can join later online)\n"
         "  --record F    write the inputs of this game to a replay file\n"
         "  --replay F    run a replay file headless (no window, no sound, as fast as possible)\n"
         "  --checksums F write one state checksum per frame to F\n"
         "  --frame-delay MS  sleep MS per frame in a headless replay (tests real-time independence)\n"
         "  --dump-objs   print the objects (type, position) when the game exits\n"
         "  --selftest F  replay F and check snapshot/restore and checksum behaviour\n\n"
         "Controls: arrows move, Ctrl jump, Alt pogo, Ctrl+Alt fire,\n"
         "Space status, F1 help, F2 sound, F3 keys, F5 save, Esc quit.");
}

int main(int argc, char **argv)
{
    const char *datadir = NULL, *replay = NULL, *checksums = NULL, *record = NULL;
    int fullscreen = 0, scale = 3, selftest = 0, local2 = 0, coop1 = 0, have_seed = 0;
    uint32_t seed = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc)
            datadir = argv[++i];
        else if (!strcmp(argv[i], "--fullscreen"))
            fullscreen = 1;
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc)
            warp_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++i], NULL, 0);
            have_seed = 1;
        } else if (!strcmp(argv[i], "--dump-objs"))
            sim_dump_at_exit();
        else if (!strcmp(argv[i], "--local2"))
            local2 = 1;
        else if (!strcmp(argv[i], "--coop"))
            coop1 = 1;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc)
            record = argv[++i];
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc)
            replay = argv[++i];
        else if (!strcmp(argv[i], "--selftest") && i + 1 < argc) {
            replay = argv[++i];
            selftest = 1;
        } else if (!strcmp(argv[i], "--checksums") && i + 1 < argc)
            checksums = argv[++i];
        else if (!strcmp(argv[i], "--frame-delay") && i + 1 < argc)
            headless_delay_ms = atoi(argv[++i]);
        else if (!strncasecmp(argv[i], "/LEVEL", 6))
            warp_level = atoi(argv[i] + 6);
        else {
            usage();
            return strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }
#if defined(__EMSCRIPTEN__) && !defined(KEEN_NODE)
    /* web/pre.js mounts IndexedDB storage here; kdata.c reads the game
     * files from /data. (The Node build, `make node`, reads real files.) */
    datadir = "/save";
#endif
    if (scale < 1)
        scale = 1;
    if (warp_level < 0 || warp_level > 16)
        warp_level = 0;
    find_data_dir(datadir);
    if (replay) {
        if (!replay_load(replay))
            return 1;
        plat_set_headless(headless_delay_ms > 0 ? 2 : 1);
    }
    if (local2) {
        nplayers = 2;
        players[0].input = IN_KEYS1;
        players[1].input = IN_KEYS2;
    }
    if (local2 || coop1)
        game.coop = 1;
    game_seed = have_seed ? seed : (uint32_t)time(NULL);

    if (!exe_load()) {
        fprintf(stderr, "Cannot read KEEN1.EXE in '%s'.\n"
                        "Point --data at the Commander Keen 1 game files.\n", data_dir());
        return 1;
    }
    if (!gfx_load()) {
        fprintf(stderr, "Cannot read the EGA graphics files in '%s'.\n", data_dir());
        return 1;
    }
    long slen;
    uint8_t *snd = load_file("SOUNDS.CK1", &slen);
    if (plat_init(scale, fullscreen) != 0)
        return 1;
    if (snd)
        snd_load(snd, slen);
    load_texts();
    load_scores();
    controls_load();
    palettes_init();
    rng_seed(game_seed);
    set_palette_black();
    plat_present();
    if (checksums && !checksums_open(checksums))
        return 1;
    if (record)
        record_open(record, 0);
    if (replay) {
        int r = replay_run(selftest);
        plat_shutdown();
        return r;
    }

    for (;;) {
        title_loop();
        play_game();
    }
}
