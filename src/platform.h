/* Platform layer: SDL2 video, PC speaker emulation, 145.6 Hz timer, keyboard. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

#define SCR_W 320
#define SCR_H 200

/* EGA 4-bit pixels shown on screen. */
extern uint8_t screen[SCR_W * SCR_H];

int plat_init(int scale, int fullscreen);
/* Headless: no window, audio or real-time pacing (replay tests). Set before
 * plat_init(); plat_ticks() then follows a virtual clock driven by plat_vsync(). */
void plat_set_headless(int on);
int plat_headless(void);
/* Fast mode (online catch-up): no drawing, sound or pacing, only the simulation. */
void plat_set_fast(int on);
extern int plat_fast;
void plat_shutdown(void);

/* EGA attribute controller palette: 16 colour registers + overscan. */
void plat_set_palette(const uint8_t regs[17]);
void plat_set_border(int v);
void plat_present(void);
void plat_screenshot(const char *path);            /* test aid: the screen to a PNG */

/* The original reprograms the PIT to 1193182/8192 Hz. */
#define TICK_HZ (1193182.0 / 8192.0)
uint32_t plat_ticks(void);
void plat_delay(int ms);
/* A real pause that lets the browser (or Node) handle events, even in headless mode. */
void plat_sleep(int ms);
void plat_set_ticks(uint32_t t);
/* Wait for n vertical retraces (70 Hz), presenting the screen. */
void plat_vsync(int n);
/* Called on every vertical retrace of a blocking screen: online play keeps its input stream going. */
extern void (*plat_idle_hook)(void);
void plat_pump(void);

/* Keyboard state indexed by IBM XT scancode (like the game's ISR table). */
extern volatile uint8_t keydown[128];
void plat_clear_keys(void);
/* bioskey(): peek=1 returns the pending key without removing it.
 * Result is (scancode << 8) | ascii, or 0 when no key is waiting. */
int plat_bioskey(int peek);
/* Every key typed since the last call or clear, oldest first (same encoding), 0 when none. */
int plat_text_key(void);
void plat_text_clear(void);
uint32_t plat_ms(void);                 /* wall clock in milliseconds, for timed messages */

/* PC speaker sounds (SOUNDS.CK1 format). */
void snd_load(const uint8_t *data, long len);
void snd_play(int num);
void snd_play_always(int num);          /* plays even while snd_muted */
void snd_stop(void);
int snd_busy(void);
void snd_wait(void);
extern int snd_enabled;
extern int snd_muted;                   /* the game's current sound source is off this player's screen */

#endif
