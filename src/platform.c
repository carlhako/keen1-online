/* SDL2 platform layer. */
#include "platform.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

uint8_t screen[SCR_W * SCR_H];
volatile uint8_t keydown[128];
int snd_enabled = 1;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t rgb[16];
static uint32_t border_rgb;
static uint64_t perf_freq, perf_base;
static int64_t tick_adjust;
static int headless;
static int64_t virt_ticks;      /* headless clock, in 1/70 s steps scaled to ticks */
static double virt_frac;

void plat_set_headless(int on) { headless = on; }
int plat_headless(void) { return headless; }

static void virt_advance(double seconds)
{
    virt_frac += seconds * TICK_HZ;
    int64_t whole = (int64_t)virt_frac;
    virt_ticks += whole;
    virt_frac -= (double)whole;
}

/* In the 200-line EGA mode the game uses, a palette register value is
 * interpreted as RGB in bits 0-2 and intensity in bit 4 (CGA colours). */
static uint32_t ega_rgb(int v)
{
    static const uint32_t cga[16] = {
        0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
        0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff,
    };
    return 0xff000000u | cga[(v & 7) | ((v & 0x10) >> 1)];
}

void plat_set_palette(const uint8_t regs[17])
{
    for (int i = 0; i < 16; i++)
        rgb[i] = ega_rgb(regs[i]);
    border_rgb = ega_rgb(regs[16]);
}

void plat_set_border(int v)
{
    border_rgb = ega_rgb(v);
}

/* ------------------------------------------------------------------ */
/* Keyboard: SDL scancode -> XT scancode + ascii                       */

#define KEYQ 32
static uint16_t keyq[KEYQ];
static int keyq_head, keyq_tail;

static int xt_scancode(SDL_Scancode s)
{
    static const struct { SDL_Scancode sdl; uint8_t xt; } map[] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03},
        {SDL_SCANCODE_3, 0x04}, {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06},
        {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08}, {SDL_SCANCODE_8, 0x09},
        {SDL_SCANCODE_9, 0x0a}, {SDL_SCANCODE_0, 0x0b}, {SDL_SCANCODE_MINUS, 0x0c},
        {SDL_SCANCODE_EQUALS, 0x0d}, {SDL_SCANCODE_BACKSPACE, 0x0e}, {SDL_SCANCODE_TAB, 0x0f},
        {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12},
        {SDL_SCANCODE_R, 0x13}, {SDL_SCANCODE_T, 0x14}, {SDL_SCANCODE_Y, 0x15},
        {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17}, {SDL_SCANCODE_O, 0x18},
        {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_LEFTBRACKET, 0x1a},
        {SDL_SCANCODE_RIGHTBRACKET, 0x1b}, {SDL_SCANCODE_RETURN, 0x1c},
        {SDL_SCANCODE_KP_ENTER, 0x1c}, {SDL_SCANCODE_LCTRL, 0x1d}, {SDL_SCANCODE_RCTRL, 0x1d},
        {SDL_SCANCODE_A, 0x1e}, {SDL_SCANCODE_S, 0x1f}, {SDL_SCANCODE_D, 0x20},
        {SDL_SCANCODE_F, 0x21}, {SDL_SCANCODE_G, 0x22}, {SDL_SCANCODE_H, 0x23},
        {SDL_SCANCODE_J, 0x24}, {SDL_SCANCODE_K, 0x25}, {SDL_SCANCODE_L, 0x26},
        {SDL_SCANCODE_SEMICOLON, 0x27}, {SDL_SCANCODE_APOSTROPHE, 0x28},
        {SDL_SCANCODE_GRAVE, 0x29}, {SDL_SCANCODE_LSHIFT, 0x2a},
        {SDL_SCANCODE_BACKSLASH, 0x2b}, {SDL_SCANCODE_Z, 0x2c}, {SDL_SCANCODE_X, 0x2d},
        {SDL_SCANCODE_C, 0x2e}, {SDL_SCANCODE_V, 0x2f}, {SDL_SCANCODE_B, 0x30},
        {SDL_SCANCODE_N, 0x31}, {SDL_SCANCODE_M, 0x32}, {SDL_SCANCODE_COMMA, 0x33},
        {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35},
        {SDL_SCANCODE_RSHIFT, 0x36}, {SDL_SCANCODE_KP_MULTIPLY, 0x37},
        {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_RALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39},
        {SDL_SCANCODE_CAPSLOCK, 0x3a}, {SDL_SCANCODE_F1, 0x3b}, {SDL_SCANCODE_F2, 0x3c},
        {SDL_SCANCODE_F3, 0x3d}, {SDL_SCANCODE_F4, 0x3e}, {SDL_SCANCODE_F5, 0x3f},
        {SDL_SCANCODE_F6, 0x40}, {SDL_SCANCODE_F7, 0x41}, {SDL_SCANCODE_F8, 0x42},
        {SDL_SCANCODE_F9, 0x43}, {SDL_SCANCODE_F10, 0x44}, {SDL_SCANCODE_NUMLOCKCLEAR, 0x45},
        {SDL_SCANCODE_SCROLLLOCK, 0x46}, {SDL_SCANCODE_HOME, 0x47}, {SDL_SCANCODE_KP_7, 0x47},
        {SDL_SCANCODE_UP, 0x48}, {SDL_SCANCODE_KP_8, 0x48}, {SDL_SCANCODE_PAGEUP, 0x49},
        {SDL_SCANCODE_KP_9, 0x49}, {SDL_SCANCODE_KP_MINUS, 0x4a}, {SDL_SCANCODE_LEFT, 0x4b},
        {SDL_SCANCODE_KP_4, 0x4b}, {SDL_SCANCODE_KP_5, 0x4c}, {SDL_SCANCODE_RIGHT, 0x4d},
        {SDL_SCANCODE_KP_6, 0x4d}, {SDL_SCANCODE_KP_PLUS, 0x4e}, {SDL_SCANCODE_END, 0x4f},
        {SDL_SCANCODE_KP_1, 0x4f}, {SDL_SCANCODE_DOWN, 0x50}, {SDL_SCANCODE_KP_2, 0x50},
        {SDL_SCANCODE_PAGEDOWN, 0x51}, {SDL_SCANCODE_KP_3, 0x51}, {SDL_SCANCODE_INSERT, 0x52},
        {SDL_SCANCODE_KP_0, 0x52}, {SDL_SCANCODE_DELETE, 0x53}, {SDL_SCANCODE_KP_PERIOD, 0x53},
        {SDL_SCANCODE_F11, 0x57}, {SDL_SCANCODE_F12, 0x58},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i].sdl == s)
            return map[i].xt;
    return 0;
}

static int key_ascii(const SDL_Keysym *k)
{
    SDL_Keycode c = k->sym;
    int shift = (k->mod & KMOD_SHIFT) != 0;
    if (c >= 'a' && c <= 'z')
        return shift ? c - 32 : c;
    if (c == SDLK_RETURN || c == SDLK_KP_ENTER)
        return 13;
    if (c == SDLK_ESCAPE)
        return 27;
    if (c == SDLK_BACKSPACE)
        return 8;
    if (c == SDLK_TAB)
        return 9;
    if (c >= 32 && c < 127) {
        if (!shift)
            return c;
        static const char from[] = "1234567890-=[];',./`\\";
        static const char to[] = "!@#$%^&*()_+{}:\"<>?~|";
        const char *p = strchr(from, c);
        return p ? to[p - from] : c;
    }
    return 0;
}

/* The game's keyboard ISR empties the BIOS buffer before chaining to the
 * BIOS handler, so only the most recent key is ever waiting. */
static void push_key(uint16_t k)
{
    keyq_tail = 0;
    keyq[0] = k;
    keyq_head = 1;
}

void plat_pump(void)
{
    if (headless)
        return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            plat_shutdown();
            exit(0);
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_RETURN &&
                (ev.key.keysym.mod & KMOD_ALT)) {
                uint32_t fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                break;
            }
            int sc = xt_scancode(ev.key.keysym.scancode);
            if (!sc)
                break;
            if (ev.type == SDL_KEYDOWN) {
                keydown[sc] = 1;
                push_key((uint16_t)(sc << 8 | key_ascii(&ev.key.keysym)));
            } else {
                keydown[sc] = 0;
            }
            break;
        }
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                memset((void *)keydown, 0, sizeof keydown);
            break;
        }
    }
}

void plat_clear_keys(void)
{
    plat_pump();
    memset((void *)keydown, 0, sizeof keydown);
}

int plat_bioskey(int peek)
{
    plat_pump();
    if (keyq_head == keyq_tail)
        return 0;
    uint16_t k = keyq[keyq_tail];
    if (!peek)
        keyq_tail = (keyq_tail + 1) % KEYQ;
    return k;
}

/* ------------------------------------------------------------------ */
/* Timer                                                               */

static int64_t raw_ticks(void)
{
    if (headless)
        return virt_ticks;
    return (int64_t)((double)(SDL_GetPerformanceCounter() - perf_base) * TICK_HZ /
                     (double)perf_freq);
}

void plat_delay(int ms)
{
    if (headless) {
        /* a real delay when asked, so tests can vary real time */
        virt_advance(ms / 1000.0);
        if (ms > 0 && headless > 1)
            SDL_Delay((uint32_t)ms);
        return;
    }
#ifdef __EMSCRIPTEN__
    emscripten_sleep((unsigned)ms);
#else
    SDL_Delay((uint32_t)ms);
#endif
}

uint32_t plat_ticks(void)
{
    return (uint32_t)(raw_ticks() + tick_adjust);
}

void plat_set_ticks(uint32_t t)
{
    tick_adjust = (int64_t)t - raw_ticks();
}

/* ------------------------------------------------------------------ */
/* Debug scripting: KEEN_SCRIPT=file with lines "<frame> down|up <scancode> [player]",
 * "<frame> shot <file.png>", "<frame> quit". Frames count presents.
 * With a player number, the scancode names a key of player 1's key set and
 * is translated to the same key of that player's set (so a script written for
 * player 1 also drives player 2). */
extern uint8_t key_dir[8], key_b1, key_b2;
extern const uint8_t key2_dir[8], key2_b1, key2_b2;

static int script_key(int sc, int player)
{
    if (player < 2)
        return sc & 0x7f;
    for (int i = 0; i < 8; i++)
        if (sc == key_dir[i])
            return key2_dir[i];
    if (sc == key_b1)
        return key2_b1;
    if (sc == key_b2)
        return key2_b2;
    return sc & 0x7f;
}

static FILE *script;
static long script_frame;
static long next_frame = -1;
static char next_cmd[256];

static void put32(FILE *f, uint32_t v)
{
    uint8_t t[4] = {v >> 24, v >> 16, v >> 8, v};
    fwrite(t, 1, 4, f);
}

static void png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    static uint32_t crctab[256];
    if (!crctab[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            crctab[i] = c;
        }
    put32(f, n);
    fwrite(type, 1, 4, f);
    if (n)
        fwrite(d, 1, n, f);
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < 4; i++)
        c = crctab[(c ^ (uint8_t)type[i]) & 0xff] ^ (c >> 8);
    for (uint32_t i = 0; i < n; i++)
        c = crctab[(c ^ d[i]) & 0xff] ^ (c >> 8);
    put32(f, c ^ 0xffffffffu);
}

static void write_png(const char *path)
{
    /* Minimal PNG writer using uncompressed deflate blocks. */
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    int w = SCR_W, h = SCR_H;
    size_t rowlen = 1 + w * 3, rawlen = rowlen * h;
    uint8_t *raw = malloc(rawlen);
    for (int y = 0; y < h; y++) {
        raw[y * rowlen] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t c = rgb[screen[y * w + x] & 15];
            uint8_t *p = raw + y * rowlen + 1 + x * 3;
            p[0] = c >> 16;
            p[1] = c >> 8;
            p[2] = c;
        }
    }
    size_t nblk = (rawlen + 65534) / 65535;
    size_t zlen = 2 + rawlen + nblk * 5 + 4;
    uint8_t *z = malloc(zlen), *zp = z;
    *zp++ = 0x78;
    *zp++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t off = 0; off < rawlen; off += 65535) {
        size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
        *zp++ = off + n >= rawlen;
        *zp++ = n & 0xff;
        *zp++ = n >> 8;
        *zp++ = ~n & 0xff;
        *zp++ = (~n >> 8) & 0xff;
        memcpy(zp, raw + off, n);
        zp += n;
    }
    uint32_t ad = b << 16 | a;
    *zp++ = ad >> 24; *zp++ = ad >> 16; *zp++ = ad >> 8; *zp++ = ad;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13] = {0, 0, w >> 8, w & 0xff, 0, 0, h >> 8, h & 0xff, 8, 2, 0, 0, 0};
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)(zp - z));
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

static void script_step(void)
{
    if (!script)
        return;
    script_frame++;
    for (;;) {
        if (next_frame < 0) {
            char line[300];
            if (!fgets(line, sizeof line, script)) {
                fclose(script);
                script = NULL;
                return;
            }
            if (sscanf(line, "%ld %255[^\n]", &next_frame, next_cmd) != 2) {
                next_frame = -1;
                continue;
            }
        }
        if (next_frame > script_frame)
            return;
        int sc, pl = 0;
        char arg[256];
        if (sscanf(next_cmd, "down %x %d", &sc, &pl) >= 1) {
            int k = script_key(sc, pl);
            keydown[k] = 1;
            push_key((uint16_t)(k << 8 | (sc == 0x1c ? 13 : sc == 0x39 ? 32 : 0)));
        } else if (sscanf(next_cmd, "up %x %d", &sc, &pl) >= 1) {
            keydown[script_key(sc, pl)] = 0;
        } else if (sscanf(next_cmd, "key %x %255s", &sc, arg) == 2) {
            push_key((uint16_t)((sc & 0x7f) << 8 | (uint8_t)arg[0]));
        } else if (sscanf(next_cmd, "shot %255s", arg) == 1) {
            write_png(arg);
        } else if (strncmp(next_cmd, "quit", 4) == 0) {
            plat_shutdown();
            exit(0);
        }
        next_frame = -1;
    }
}

void plat_present(void)
{
    uint32_t *pix;
    int pitch;
    if (headless)
        return;
    script_step();
    if (SDL_LockTexture(tex, NULL, (void **)&pix, &pitch) == 0) {
        for (int y = 0; y < SCR_H; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)pix + y * pitch);
            const uint8_t *src = screen + y * SCR_W;
            for (int x = 0; x < SCR_W; x++)
                row[x] = rgb[src[x] & 15];
        }
        SDL_UnlockTexture(tex);
    }
    SDL_SetRenderDrawColor(ren, (border_rgb >> 16) & 0xff, (border_rgb >> 8) & 0xff,
                           border_rgb & 0xff, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

void plat_vsync(int n)
{
    if (headless) {
        virt_advance(n / 70.0);
        return;
    }
    static uint64_t last;
    uint64_t period = perf_freq / 70;
    while (n-- > 0) {
        plat_pump();
        plat_present();
        uint64_t now = SDL_GetPerformanceCounter();
        if (last == 0 || now - last > period * 4)
            last = now;
        uint64_t target = last + period;
#ifdef __EMSCRIPTEN__
        /* ASYNCIFY unwinds the game's blocking loops here and returns to the
         * browser, which can then draw the canvas and deliver input. */
        now = SDL_GetPerformanceCounter();
        emscripten_sleep(now < target ? (unsigned)((target - now) * 1000 / perf_freq) : 0);
#else
        while ((now = SDL_GetPerformanceCounter()) < target) {
            uint64_t ms = (target - now) * 1000 / perf_freq;
            if (ms > 1)
                SDL_Delay((uint32_t)ms - 1);
        }
#endif
        last = target;
    }
}

/* ------------------------------------------------------------------ */
/* PC speaker                                                          */

#define AUDIO_RATE 48000
static const uint8_t *snd_data;
static long snd_len;
static SDL_AudioDeviceID audio_dev;
static volatile long snd_pos;       /* byte offset in snd_data, 0 = idle */
static volatile int snd_prio;
static double tick_phase;           /* fraction of a timer tick elapsed */
static double tone_phase;
static double tone_step;            /* 0 = speaker off */

static void timer_tick(void)
{
    /* Mirrors the game's timer interrupt sound code. */
    if (snd_pos) {
        uint16_t w = snd_data[snd_pos] | snd_data[snd_pos + 1] << 8;
        snd_pos += 2;
        if (w == 0xffff) {
            snd_pos = 0;
            snd_prio = 0;
            tone_step = 0;
            return;
        }
        if (w != 0 && snd_enabled) {
            tone_step = (1193182.0 / w) / AUDIO_RATE;
            return;
        }
    }
    tone_step = 0;
}

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int n = len / 2;
    double tick_step = TICK_HZ / AUDIO_RATE;
    for (int i = 0; i < n; i++) {
        tick_phase += tick_step;
        if (tick_phase >= 1.0) {
            tick_phase -= 1.0;
            timer_tick();
        }
        int16_t v = 0;
        if (tone_step > 0) {
            tone_phase += tone_step;
            if (tone_phase >= 1.0)
                tone_phase -= (int)tone_phase;
            v = tone_phase < 0.5 ? 3000 : -3000;
        }
        out[i] = v;
    }
}

void snd_load(const uint8_t *data, long len)
{
    snd_data = data;
    snd_len = len;
}

void snd_play(int num)
{
    if (headless)
        return;
    if (!snd_data || num * 16 + 3 >= snd_len)
        return;
    const uint8_t *e = snd_data + num * 16;
    int prio = e[2];
    SDL_LockAudioDevice(audio_dev);
    if (prio >= snd_prio) {
        snd_prio = prio;
        snd_pos = e[0] | e[1] << 8;
    }
    SDL_UnlockAudioDevice(audio_dev);
}

void snd_stop(void)
{
    SDL_LockAudioDevice(audio_dev);
    snd_pos = 0;
    snd_prio = 0;
    tone_step = 0;
    SDL_UnlockAudioDevice(audio_dev);
}

int snd_busy(void)
{
    return snd_pos != 0;
}

void snd_wait(void)
{
    while (snd_busy() && audio_dev)
        plat_vsync(1);
}

/* ------------------------------------------------------------------ */

#ifdef __EMSCRIPTEN__
/* The page sizes the canvas with CSS; a resizable SDL window follows it at
 * the device pixel ratio, so the renderer scales straight to screen pixels. */
#define WINDOW_FLAGS SDL_WINDOW_ALLOW_HIGHDPI
#else
#define WINDOW_FLAGS 0
#endif

int plat_init(int scale, int fullscreen)
{
    if (headless) {
        if (SDL_Init(SDL_INIT_TIMER) != 0) {
            fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return -1;
        }
        perf_freq = SDL_GetPerformanceFrequency();
        perf_base = SDL_GetPerformanceCounter();
        return 0;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    win = SDL_CreateWindow("Commander Keen 1: Marooned on Mars", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, 320 * scale, 240 * scale,
                           SDL_WINDOW_RESIZABLE | WINDOW_FLAGS |
                               (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    /* 320x200 shown at 4:3 like a CRT. */
    SDL_RenderSetLogicalSize(ren, 320, 240);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCR_W,
                            SCR_H);
    SDL_ShowCursor(SDL_DISABLE);

    perf_freq = SDL_GetPerformanceFrequency();
    perf_base = SDL_GetPerformanceCounter();
    if (getenv("KEEN_SCRIPT"))
        script = fopen(getenv("KEEN_SCRIPT"), "r");

    SDL_AudioSpec want = {0}, have;
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_cb;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev)
        SDL_PauseAudioDevice(audio_dev, 0);
    else
        fprintf(stderr, "No audio: %s\n", SDL_GetError());
    return 0;
}

void plat_shutdown(void)
{
    if (headless) {
        SDL_Quit();
        return;
    }
    if (audio_dev)
        SDL_CloseAudioDevice(audio_dev);
    audio_dev = 0;
    SDL_Quit();
}
