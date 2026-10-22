/* Title sequence, main menu, world map, ending, high scores, save/load
 * (KEEN1.EXE 0x548B, 0x6854, 0x7B0A..0xB30D). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "gfx.h"
#include "kdata.h"
#include "platform.h"

extern void reset_frame_time(void);
extern uint8_t key_dir[8], key_b1, key_b2;

/* A 32 bit value stored as two words in the EXE's data (low word first). The
 * low word must not be sign-extended into the high one. */
static int32_t ds_l(unsigned off)
{
    return (int32_t)((uint16_t)ds_w(off) | (uint32_t)(uint16_t)ds_w(off + 2) << 16);
}

static int first_run = 1;      /* DS:2870, plays the Apogee intro once */
static int game_loaded;        /* DS:A5BA */
static int ctrl_type;          /* DS:7D34, 0 = keyboard */
static int sel_level;          /* DS:6CF0 */
static int block_mask = 0x8000;/* DS:A214 */
static int menu_x, menu_y;     /* DS:A620/A624 */
static int attract_skip;       /* DS:A224 */

/* ------------------------------------------------------------------ */
/* High scores (SCORES.CK1, 204 bytes at DS:A62A) */

#pragma pack(push, 1)
static struct {
    int32_t score[7];
    int16_t joystick[7], battery[7], whiskey[7], vacuum[7];
    int16_t unused1[7], unused2[7];
    char name[7][13];
    char pad;
} hs;
#pragma pack(pop)

void load_scores(void)
{
    long len;
    uint8_t *d = load_file("SCORES.CK1", &len);
    if (d && len >= (long)sizeof hs - 1) {
        memcpy(&hs, d, sizeof hs - 1);
        free(d);
        return;
    }
    free(d);
    memset(&hs, 0, sizeof hs);
    for (int i = 0; i < 7; i++) {
        hs.score[i] = 100;
        snprintf(hs.name[i], 13, "%s", ds_str(0x28b3 + (i % 3) * 15));
    }
    save_file("SCORES.CK1", &hs, 0xcc);
}

/* ------------------------------------------------------------------ */
/* CTLPANEL.CK1 (sound, input device, key bindings) */

static uint8_t ctlpanel[44];

void controls_load(void)
{
    long len;
    uint8_t *d = load_file("CTLPANEL.CK1", &len);
    if (!d || len < 44) {
        free(d);
        return;
    }
    memcpy(ctlpanel, d, 44);
    free(d);
    snd_enabled = ctlpanel[0];
    ctrl_type = 0; /* keyboard only in this port */
    memcpy(key_dir, ctlpanel + 34, 8);
    key_b1 = ctlpanel[42];
    key_b2 = ctlpanel[43];
}

static void controls_save(void)
{
    ctlpanel[0] = (uint8_t)snd_enabled;
    ctlpanel[1] = 0;
    memcpy(ctlpanel + 34, key_dir, 8);
    ctlpanel[42] = key_b1;
    ctlpanel[43] = key_b2;
    save_file("CTLPANEL.CK1", ctlpanel, 44);
}

static const char *key_name(int sc)
{
    static const struct { int sc; unsigned off; } names[] = {
        {0x01, 0x25dd}, {0x0e, 0x25e1}, {0x0f, 0x25e6}, {0x1d, 0x25ea}, {0x2a, 0x25ef},
        {0x39, 0x25f6}, {0x3a, 0x25fc}, {0x57, 0x2605}, {0x58, 0x2609}, {0x46, 0x260d},
        {0x1c, 0x2614}, {0x36, 0x261a}, {0x37, 0x2621}, {0x38, 0x2627}, {0x47, 0x262b},
        {0x49, 0x2630}, {0x4f, 0x2635}, {0x51, 0x2639}, {0x52, 0x263e}, {0x53, 0x2642},
        {0x45, 0x2646},
    };
    static char buf[8];
    static const char *scan = "  1234567890-=  QWERTYUIOP[]  ASDFGHJKL;'` \\ZXCVBNM,./";
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (names[i].sc == sc)
            return ds_str(names[i].off);
    if (sc >= 0x3b && sc <= 0x44) {
        snprintf(buf, sizeof buf, "F%d", sc - 0x3a);
        return buf;
    }
    if (sc == 0x48) return "UP";
    if (sc == 0x50) return "DOWN";
    if (sc == 0x4b) return "LEFT";
    if (sc == 0x4d) return "RIGHT";
    if (sc < (int)strlen(scan) && scan[sc] != ' ') {
        buf[0] = scan[sc];
        buf[1] = 0;
        return buf;
    }
    snprintf(buf, sizeof buf, "%02X", sc);
    return buf;
}

/* F3: keyboard configuration (0x548B, simplified to the keyboard part). */
void controls_menu(void)
{
    for (;;) {
        win_grow(0x1c, 0xd);
        int x0 = cur_x;
        print(ds_str(0x264c));
        print(ds_str(0x265f));
        for (int i = 0; i < 10; i++) {
            print(ds_str(0x2671 + i * 14));
            int sc = i < 8 ? key_dir[i] : (i == 8 ? key_b1 : key_b2);
            print(key_name(sc));
        }
        print(ds_str(0x26fd));
        int c = wait_key();
        if (c < '0' || c > '9')
            break;
        print(ds_str(0x2713));
        flush_input();
        int k;
        do {
            k = key_get() >> 8;
        } while (!k);
        if (k == 0x01)
            break;
        int i = c - '0';
        if (i < 8)
            key_dir[i] = (uint8_t)k;
        else if (i == 8)
            key_b1 = (uint8_t)k;
        else
            key_b2 = (uint8_t)k;
        (void)x0;
        flush_input();
    }
    controls_save();
}

/* ------------------------------------------------------------------ */
/* Save / load (0x9CE9 / 0x9E83) */

static char save_name[16];

static void set_save_name(int c)
{
    snprintf(save_name, sizeof save_name, "SAVED%c.CK1", c);
}

void save_game_prompt(void)
{
    if (!in_world_map) {
        win_grow(0x16, 3);
        print(ds_str(0x3094));
        print(ds_str(0x30ab));
        print(ds_str(0x30c3));
        flush_input();
        wait_key();
        return;
    }
    int c;
    for (;;) {
        win_grow(0x14, 3);
        print(ds_str(0x30d4));
        print(ds_str(0x30e9));
        print(ds_str(0x30ff));
        do {
            c = wait_key();
        } while (c != 0x1b && (c < '1' || c > '9'));
        if (c == 0x1b)
            return;
        set_save_name(c);
        if (!file_exists(save_name))
            break;
        win_grow(0x14, 3);
        print(ds_str(0x310f));
        print(ds_str(0x3123));
        print(ds_str(0x3134));
        int a;
        do {
            a = toupper(wait_key());
        } while (a != 0x1b && a != 'Y' && a != 'N');
        if (a == 0x1b)
            return;
        if (a == 'Y')
            break;
    }
    save_file(save_name, &gs, sizeof gs);
    win_grow(0x1d, 3);
    print(ds_str(0x3143));
    print(ds_str(0x315f));
    print(ds_str(0x317d));
    wait_key();
}

int load_game_prompt(void)
{
    draw_char(cur_x, cur_y * 8, ' ');
    for (;;) {
        win_grow(0x19, 2);
        menu_x = cur_x;
        menu_y = cur_y;
        print(ds_str(0x3194));
        print(ds_str(0x31ac));
        int c;
        do {
            c = wait_key();
        } while (c != 0x1b && (c < '1' || c > '9'));
        if (c == 0x1b)
            return 0;
        set_save_name(c);
        long len;
        uint8_t *d = load_file(save_name, &len);
        if (!d) {
            win_grow(0x19, 2);
            print(ds_str(0x31bf));
            print(ds_str(0x31d3));
            wait_key();
            continue;
        }
        if (len == (long)sizeof gs) {
            memcpy(&gs, d, sizeof gs);
            free(d);
            game_loaded = 1;
            return 1;
        }
        free(d);
        win_grow(0x19, 2);
        print(ds_str(0x31e6));
        print(ds_str(0x3201));
        wait_key();
    }
}

/* A game that starts from a record (a save held by the server) rather than a new one. */
void game_load_record(const void *rec)
{
    memcpy(&gs, rec, sizeof gs);
    game_loaded = 1;
}

/* Online, F5 on the world map saves the shared game on the server. The other players are paused
 * (the caller sent EV_SAVE_ON); this machine asks for a name, sends the record and says how it went. */
#define SAVE_NAME_MAX 20

void online_save_prompt(void)
{
    char def[SAVE_NAME_MAX + 1], name[SAVE_NAME_MAX + 1];
    int levels = 0;
    for (int i = 0; i < 16; i++)
        levels += gs.done[i] != 0;
    snprintf(def, sizeof def, "GAME %d - %d LEVELS", net_game_id(), levels);
    win_grow(0x20, 5);
    print("SAVE GAME AS:\n");
    print_hi(def);
    print("\nENTER USES THIS NAME\nESC STOPS\n");
    if (!input_line(name, SAVE_NAME_MAX))
        return;
    name[SAVE_NAME_MAX] = 0;
    if (!name[0])
        snprintf(name, sizeof name, "%s", def);
    int stored = net_save_record(name);
    win_grow(0x10, 1);
    print(stored ? "Saved" : "Save failed");
    flush_input();
    wait_key();
}

/* ------------------------------------------------------------------ */
/* Title screen pages (render hooks) */

static void hook_title(void)
{
    draw_bitmap(8, 1, 0);
    draw_bitmap(0x10, 0xb6, 2);
}

static void hook_about(void)
{
    static const unsigned text[] = {0x2f3d, 0x2f61, 0x2f84, 0x2fa4, 0x2fbb, 0x2fdd,
                                    0x3002, 0x301e, 0x3038, 0x3057, 0x307a};
    draw_bitmap(0x11, 0x17, 1);
    margin_x = cur_x = 7;
    cur_y = 9;
    for (size_t i = 0; i < sizeof text / sizeof text[0]; i++)
        print_hi(ds_str(text[i]));
}

static void hook_scores(void)
{
    draw_bitmap(0xf, 7, 3);
    draw_bitmap(9, 0x2c, 4);
    draw_bitmap(0x17, 0x2c, 5);
    draw_bitmap(0x21, 0x2c, 6);
    int tx = scroll_x >> 12, ty = scroll_y >> 12;
    for (int i = 0; i < 7; i++) {
        int f = i % 4;
        cur_y = i * 2 + 8;
        if (hs.joystick[i]) set_tile(tx + 0xe, cur_y / 2 + ty, f + 0xdd);
        if (hs.battery[i]) set_tile(tx + 0xf, cur_y / 2 + ty, f + 0xed);
        if (hs.vacuum[i]) set_tile(tx + 0x10, cur_y / 2 + ty, f + 0xf1);
        if (hs.whiskey[i]) set_tile(tx + 0x11, cur_y / 2 + ty, f + 0xf5);
        char num[16];
        snprintf(num, sizeof num, "%ld", (long)hs.score[i]);
        cur_x = 0x1d - (int)strlen(num);
        print_hi(num);
        cur_x = 9;
        char name[14];
        memcpy(name, hs.name[i], 13);
        name[13] = 0;
        print_hi(name);
    }
}

static void hook_order(void)
{
    static const unsigned head[] = {0x3240, 0x325a, 0x3275, 0x328d, 0x32a7, 0x32c2, 0x32dd};
    static const unsigned body[] = {0x32f9, 0x3322, 0x3349, 0x3370, 0x3395,
                                    0x33b5, 0x33dd, 0x3405, 0x342d};
    /* v1.31 (DS offsets of that version) adds a disk size line. */
    static const unsigned head131[] = {0x32dc, 0x32f6, 0x3311, 0x3329, 0x3342, 0x335d, 0x3378};
    static const unsigned body131[] = {0x33bb, 0x33e2, 0x3409, 0x342e, 0x344e,
                                       0x3476, 0x349e, 0x34c6, 0x34f2};
    margin_x = cur_x = 0xc;
    cur_y = 4;
    if (game_version == 131) {
        for (int i = 0; i < 7; i++)
            print_hi(ds_str_native(head131[i]));
        margin_x = cur_x = 5;
        print_hi(ds_str_native(0x3393));
        margin_x = --cur_x;
        for (int i = 0; i < 9; i++)
            print_hi(ds_str_native(body131[i]));
        cur_x = 0;
        print_hi(ds_str_native(0x351c));
        return;
    }
    for (int i = 0; i < 7; i++)
        print_hi(ds_str(head[i]));
    margin_x = cur_x = 4;
    for (int i = 0; i < 9; i++)
        print_hi(ds_str(body[i]));
    cur_x = 0;
    print_hi(ds_str(0x3458));
}

/* 0x9805: the main menu window, drawn over the title page. */
static void draw_menu(void)
{
    win_grow(0x12, 10);
    menu_x = cur_x;
    menu_y = cur_y;
    static const unsigned items[] = {0x2e89, 0x2e96, 0x2ea8, 0x2eb2,
                                     0x2ec2, 0x2ed2, 0x2ee4, 0x2ef2};
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++)
        print(ds_str(items[i]));
    print(ds_str(0x2f04));
    print(ds_str(ctrl_type == 0 ? 0x2f0d : ctrl_type == 1 ? 0x2f14 : 0x2f1a));
    cur_x = menu_x + 1;
    cur_y = menu_y;
}

/* The original draws these pages into both video pages once; with a full
 * redraw every frame the hook simply stays active while the page is up. */
static void show_page(void (*hook)(void), int32_t sx, int32_t sy)
{
    scroll_x = sx;
    scroll_y = sy;
    rf_hook = hook;
    frame_start();
    frame_refresh();
    frame_refresh();
    flush_input();
}

static void page_title(void) { show_page(hook_title, 0x2000, 0x2000); }
static void page_about(void) { show_page(hook_about, 0x3f000, 0x1c00); }
static void page_scores(void) { show_page(hook_scores, 0x54000, 0x2000); }
static void page_order(void) { show_page(hook_order, 0x16000, 0x2000); }

/* 0xA41A: "Apogee presents / an id Software production". */
static int intro_y;

static void hook_intro1(void)
{
    draw_bitmap(0x10, intro_y, 10);
}

static void hook_intro2(void)
{
    draw_bitmap(0x16, intro_y - 10, 8);
    draw_bitmap(0x10, intro_y, 10);
    draw_bitmap(0x12, intro_y + 0x20, 9);
    draw_bitmap(0x15, 99, 0xf);
    draw_bitmap(0x13, 0x72, 0x11);
    draw_bitmap(0x13, 0x9f, 0x10);
}

static void intro(void)
{
    scroll_x = 0x68000;
    scroll_y = 0x2000;
    int frames = 300, phase = 0, wait = 0;
    intro_y = 200;
    rf_hook = NULL;
    frame_start();
    frame_refresh();
    fade_in();
    flush_input();
    attract_skip = 0;
    while (frames-- > 0) {
        frame_start();
        frame_refresh();
        if (phase == 0) {
            if (++wait > 0x1e) {
                phase = 1;
                rf_hook = hook_intro1;
            }
        } else if (phase == 1) {
            if (intro_y < 0x38) {
                phase = 2;
                rf_hook = hook_intro2;
            } else {
                intro_y--;
            }
        }
        ctrl_t c;
        read_ctrl(&c);
        if (c.b1 || c.b2 || key_peek()) {
            attract_skip = 1;
            break;
        }
    }
    rf_hook = NULL;
}

/* 0xA4F3: ordering info with a Yorp or Garg dancing in the corner. */
static void order_screen(int timed)
{
    static const int base[2] = {48, 60};
    static const int32_t ox[2] = {0x9000, 0x8d00}, oy[2] = {0x9f00, 0x9700};
    int frame = 0, count = 0, which = 0, n = 0, timer = 0x960;
    page_order();
    fade_in();
    flush_input();
    do {
        ctrl_t c;
        read_ctrl(&c);
        frame_start();
        if (n % 6 == 0) {
            frame += rand_range(3) - 2;
            if (frame > 7 || frame < 0)
                frame = rand_range(0x31) / 7;
        }
        if (++count > 500) {
            which ^= 1;
            count = 0;
        }
        rf_sprite(scroll_x + ox[which], scroll_y + oy[which], base[which] + frame);
        rf_hook = hook_order;
        frame_refresh();
        rf_hook = NULL;
        n++;
        if (timed)
            timer -= tics;
        if (check_hotkeys())
            flush_input();
        if (c.b1 || c.b2 || key_peek())
            timer = 0;
    } while (timer > 0);
}

static void previews(void)
{
    show_pic("PREVIEW2.CK1");
    fade_in();
    wait_vbl(300);
    fade_out();
    show_pic("PREVIEW3.CK1");
    fade_in();
    wait_vbl(300);
    text_viewer(txt_previews, 0, 0x16);
    load_level(0x5a);
}

static void story(void)
{
    fade_out();
    rf_hook = NULL;
    scroll_x = 0x2a000;
    scroll_y = 0x2000;
    frame_start();
    frame_refresh();
    fade_in();
    text_viewer(txt_story, 0, 0x10);
    fade_out();
}

static void wait_page(void (*page)(void))
{
    page();
    fade_in();
    flush_input();
    for (;;) {
        frame_start();
        frame_refresh();
        if (check_hotkeys())
            page();
        ctrl_t c;
        read_ctrl(&c);
        if (c.b1 || c.b2 || key_peek())
            break;
    }
}

/* 0x949D: main menu. Returns 1 to start a game. */
static int main_menu(void)
{
    for (;;) {
        int faded = scroll_x != 0x2000 || scroll_y != 0x2000;
        show_page(hook_title, 0x2000, 0x2000);
        draw_menu();
        if (faded)
            fade_in();
        int sel = 0, anim = 0, repeat = 0x14, done = 0;
        ctrl_t c;
        cur_x = menu_x;
        cur_y = menu_y;
        for (;;) {
            for (int i = 0; i < 6; i++) {
                wait_vbl(1);
                read_ctrl(&c);
                if (c.dir != 8)
                    break;
            }
            draw_char(cur_x, cur_y * 8, anim + 9);
            if (++anim > 2)
                anim = 0;
            if (c.dir == 0 || c.dir == 4) {
                int up = c.dir == 0;
                if (up && sel == 0) {
                    sel = 7;
                    draw_char(cur_x, cur_y * 8, ' ');
                } else if (!up && sel >= 7) {
                    sel = 0;
                    draw_char(cur_x, cur_y * 8, ' ');
                } else {
                    sel += up ? -1 : 1;
                    for (int i = 0; i < 8; i++) {
                        int y = cur_y * 8 + (up ? -i : i);
                        draw_char(cur_x, y, anim + 9);
                        wait_vbl(1);
                        draw_char(cur_x, y, ' ');
                        if (++anim > 2)
                            anim = 0;
                    }
                }
                cur_y = menu_y + sel;
                draw_char(cur_x, cur_y * 8, anim + 9);
                while (repeat && c.dir != 8) {
                    repeat--;
                    read_ctrl(&c);
                    wait_vbl(1);
                }
                repeat = 0x14;
            } else {
                wait_vbl(7);
            }
            int k = key_peek() & 0xff;
            if ((k == ' ' || k == '\r') || c.b1 || c.b2)
                done = 1;
            if (!done && check_hotkeys()) {
                show_page(hook_title, 0x2000, 0x2000);
                draw_menu();
                cur_y = menu_y + sel;
            }
            if (done)
                break;
        }
        flush_input();
        wait_vbl(8);
        int start = 0;
        switch (sel) {
        case 0:
            start = 1;
            break;
        case 1:
            start = load_game_prompt();
            break;
        case 2:
            story();
            break;
        case 3:
            fade_out();
            wait_page(page_about);
            fade_out();
            break;
        case 4:
            fade_out();
            wait_page(page_scores);
            fade_out();
            break;
        case 5:
            fade_out();
            order_screen(0);
            fade_out();
            break;
        case 6:
            fade_out();
            previews();
            fade_out();
            break;
        case 7:
            return 0;
        }
        if (start)
            return 1;
    }
}

/* A new game's record: four lives, no ammo, nothing found. */
void game_new(void)
{
    gs.lives = 4;
    gs.ammo = 0;
    gs.pogo = gs.joystick = gs.battery = gs.vacuum = gs.whiskey = 0;
    memset(gs.unused, 0, sizeof gs.unused);
    memset(gs.keys, 0, sizeof gs.keys);
    memset(gs.done, 0, sizeof gs.done);
    game_loaded = 0;
}

/* 0x91F5: attract loop -> menu. */
void title_loop(void)
{
    game_new();
    if (warp_level)
        return;
    for (;;) {
        anim_shift = 3;
        load_level(0x5a);
        if (first_run) {
            intro();
            fade_out();
            first_run = 0;
        }
        int page = 0, timer = 0x960;
        if (!attract_skip) {
            page_title();
            fade_in();
            flush_input();
            for (;;) {
                ctrl_t c;
                read_ctrl(&c);
                frame_start();
                frame_refresh();
                if (check_hotkeys()) {
                    if (page == 0) page_title();
                    else if (page == 1) page_about();
                    else if (page == 2) page_scores();
                    else page_order();
                }
                timer -= tics;
                if (timer < 1) {
                    fade_out();
                    if (page == 0) {
                        page_about();
                    } else if (page == 1) {
                        page_scores();
                    } else if (page == 2) {
                        page = 3;
                        order_screen(1);
                        read_ctrl(&c);
                        if (key_peek() || c.b1 || c.b2)
                            break;
                        fade_out();
                    }
                    timer = 0x960;
                    if (++page == 4) {
                        page = 0;
                        page_title();
                    }
                    fade_in();
                }
                if (key_peek() || c.b1 || c.b2)
                    break;
            }
        }
        attract_skip = 0;
        if (scroll_x != 0x2000 || scroll_y != 0x2000)
            fade_out();
        if (main_menu()) {
            flush_input();
            fade_out();
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* World map */

static struct { int x, y, wide; } level_pos[17];
static int32_t map_sx, map_sy;      /* DS:A226.. working copy of the map scroll */

/* 0x826D: locate the level entrances on the map. */
static void find_levels(void)
{
    memset(level_pos, 0, sizeof level_pos);
    for (int ty = 0; ty < map_h; ty++)
        for (int tx = 0; tx < map_w; tx++) {
            int v = MAPI(tx, ty) & 0x7fff;
            if (v && v < 17 && level_pos[v].x == 0) {
                level_pos[v].x = tx;
                level_pos[v].y = ty;
                level_pos[v].wide = 0;
                if ((MAPI(tx + 1, ty) & 0x7fff) == v) {
                    tx++;
                    level_pos[v].wide = 1;
                }
            }
        }
}

/* 0x912B */
static void keens_left(void)
{
    win_grow(0x10, 4);
    int x0 = cur_x;
    print(ds_str(0x2e7b));
    int n = gs.lives > 6 ? 6 : gs.lives;
    for (int i = 0; i < n; i++)
        draw_sprite_at(x0 + i * 2 + 1, cur_y * 8 + 0xb, 0x90);
    snd_play(0x29);
}

/* 0xA79D: tile collision on the world map (blocks also on info & mask). */
static int map_clip(obj_t *k)
{
    if (godmode)
        return 0;
    obj_t t = *k;
    int flag = 0;
    t.y2 += t.dy;
    t.y1 += t.dy;
    int tx1 = t.x1 / 0x1000, tx2 = t.x2 / 0x1000;
    if (t.dy < 0) {
        if (t.y1 / 0x1000 != (t.y1 - t.dy) / 0x1000) {
            int ty = t.y1 / 0x1000;
            for (int tx = tx1; tx <= tx2; tx++)
                if (T_BOTTOM(tile_at(tx, ty)) || (MAPI(tx, ty) & block_mask)) {
                    k->yv = 0;
                    int m = 0x1000 - (int)(t.y1 % 0x1000);
                    k->dy += m;
                    t.y1 += m;
                    t.y2 += m;
                    flag = 1;
                    break;
                }
        }
    } else if (t.dy > 0) {
        if (t.y2 / 0x1000 != (t.y2 - t.dy) / 0x1000) {
            int ty = t.y2 / 0x1000;
            for (int tx = tx1; tx <= tx2; tx++)
                if (T_TOP(tile_at(tx, ty)) || (MAPI(tx, ty) & block_mask)) {
                    k->yv = 0;
                    int m = (int)((t.y2 + 1) % 0x1000);
                    k->dy -= m;
                    t.y1 -= m;
                    t.y2 -= m;
                    flag = 1;
                    break;
                }
        }
    }
    t.x1 += t.dx;
    t.x2 += t.dx;
    int ty1 = t.y1 / 0x1000, ty2 = t.y2 / 0x1000;
    if (t.dx < 0) {
        if (t.x1 / 0x1000 != (t.x1 - t.dx) / 0x1000) {
            int tx = t.x1 / 0x1000;
            for (int ty = ty1; ty <= ty2; ty++)
                if (T_RIGHT(tile_at(tx, ty)) || (MAPI(tx, ty) & block_mask)) {
                    k->xv = 0;
                    int m = 0x1000 - (int)(t.x1 % 0x1000);
                    k->dx += m;
                    return 1;
                }
        }
    } else if (t.dx > 0) {
        if (t.x2 / 0x1000 != (t.x2 - t.dx) / 0x1000) {
            int tx = t.x2 / 0x1000;
            for (int ty = ty1; ty <= ty2; ty++)
                if (T_LEFT(tile_at(tx, ty)) || (MAPI(tx, ty) & block_mask)) {
                    k->xv = 0;
                    int m = (int)((t.x2 + 1) % 0x1000);
                    k->dx -= m;
                    return 1;
                }
        }
    }
    return flag;
}

static void teleport_anim(int tx, int ty, int flag)
{
    int base = flag ? 0x156 : 0x152, a = 0;
    for (int i = 0; i < 16; i++) {
        frame_start();
        if (i % 2 == 0 && ++a > 3)
            a = 0;
        set_tile(tx, ty, a + base);
        frame_refresh();
    }
    set_tile(tx, ty, flag ? 99 : 0x145);
}

static void wait_buttons_up_map(obj_t *k)
{
    ctrl_t c;
    do {
        frame_start();
        read_ctrl(&c);
        draw_keen(k->x, k->y, k->sprite, player_of(k));
        frame_refresh();
        check_cheats();
        check_hotkeys();
    } while (c.b1 || c.b2);
}

/* 0xAFA4: the ship and the teleporters. */
static int map_special(int sel, obj_t *k, int lx, int ly)
{
    if (sel == 0x14) {
        win_grow(0x14, 8);
        ship_paint(ctrl_type);
        wait_vbl(0xf);
        flush_input();
        int a = 0;
        for (;;) {
            draw_char(cur_x, cur_y * 8, a + 9);
            ctrl_t c;
            int hit = 0;
            for (int i = 0; i < 6 && !hit; i++) {
                wait_vbl(1);
                read_ctrl(&c);
                if (c.b1 || c.b2 || key_peek())
                    hit = 1;
            }
            if (++a > 4)
                a = 0;
            if (hit) {
                do {
                    read_ctrl(&c);
                    wait_vbl(1);
                } while (c.b1 || c.b2);
                flush_input();
                break;
            }
        }
        return 1;
    }
    if ((sel & 0x20) == 0x20) {
        int dst = (sel & 3) - 1, src = ((sel >> 2) & 3) - 1;
        int32_t tx = ds_l(0x2852 + dst * 10);
        int32_t ty = ds_l(0x2856 + dst * 10);
        snd_play(0x12);
        teleport_anim(lx, ly, ds_w(0x285a + src * 10));
        int pi = player_of(k);
        k->x = players[pi].map_x = tx;
        k->y = players[pi].map_y = ty;
        if (pi == local_player) {
            scroll_x = k->x - 0x9000;
            scroll_y = k->y - 0x3000;
        }
        teleport_anim(k->x / 0x1000, k->y / 0x1000, ds_w(0x285a + dst * 10));
        wait_buttons_up_map(k);
        return 1;
    }
    return 0;
}

/* 0xAC1C: Keen walking on the world map. */
static void map_keen(int pi)
{
    static int lxs[MAXPLAYERS], lys[MAXPLAYERS];
    int *lxp = &lxs[pi], *lyp = &lys[pi];
    obj_t *k = &objs[pi];
    const ctrl_t *c = &players[pi].ctrl;
    calc_hitbox_obj(k);
    if (c->b1 || c->b2) {
        int tx1 = k->x1 / 0x1000, tx2 = k->x2 / 0x1000;
        int ty1 = k->y1 / 0x1000, ty2 = k->y2 / 0x1000;
        for (int tx = tx1; tx <= tx2; tx++)
            for (int ty = ty1; ty <= ty2; ty++)
                if (MAPI(tx, ty)) {
                    *lxp = tx;
                    *lyp = ty;
                    sel_level = MAPI(tx, ty);
                    if (sel_level == 0xff)
                        sel_level = 0;
                }
    }
    k->dy = k->dx = 0;
    switch (c->dir) {
    case 0: k->dy = -0x400; k->sprite = 0x2c; break;
    case 1: k->dy = -0x400; k->dx = 0x400; k->sprite = 0x2c; break;
    case 2: k->dx = 0x400; k->sprite = 0x20; break;
    case 3: k->dy = 0x400; k->dx = 0x400; k->sprite = 0x24; break;
    case 4: k->dy = 0x400; k->sprite = 0x24; break;
    case 5: k->dy = 0x400; k->dx = -0x400; k->sprite = 0x24; break;
    case 6: k->dx = -0x400; k->sprite = 0x28; break;
    case 7: k->dy = -0x400; k->dx = -0x400; k->sprite = 0x2c; break;
    }
    uint32_t tick = gtick;
    int moving = k->dx || k->dy;
    int frame = moving ? (tick >> 4) & 3 : 0;
    block_mask = 0x8000;
    if (keydown[0x0f] && keydown[0x2a])
        block_mask = 0;
    int blocked = map_clip(k);
    k->x += k->dx;
    k->y += k->dy;
    if (moving && ((tick >> 3) & 3) == 0)
        snd_play(blocked ? 2 : 1);
    /* The map scrolls to follow the local player only. */
    if (pi == local_player) {
        int32_t kx = k->x - scroll_x, ky = k->y - scroll_y;
        if (k->dx > 0 && kx > 0xb000) {
            scroll_x += k->dx;
            if (scroll_x > scrollmax_x)
                scroll_x = scrollmax_x;
        } else if (k->dx < 0 && kx < 0x9000) {
            scroll_x += k->dx;
            if (scroll_x < scrollmin_x)
                scroll_x = scrollmin_x;
        }
        if (k->dy > 0 && ky > 0x7000) {
            scroll_y += k->dy;
            if (scroll_y > scrollmax_y)
                scroll_y = scrollmax_y;
        } else if (k->dy < 0 && ky < 0x3000) {
            scroll_y += k->dy;
            if (scroll_y < scrollmin_y)
                scroll_y = scrollmin_y;
        }
    }
    draw_keen(k->x, k->y, k->sprite + frame, pi);
    if (sel_level && map_special(sel_level, k, *lxp, *lyp))
        sel_level = 0;
}

/* ------------------------------------------------------------------ */
/* Co-op world map: every player walks his own Keen with his own camera, the
 * level starts when everybody has pressed on the same entrance, and the ship
 * and the teleporters act on one player at a time. */

/* Camera centred on player p's Keen, within the map. */
void map_camera_center(int p)
{
    player_t *pl = &players[p];
    pl->cam_x = objs[p].x - 0x9000;
    pl->cam_y = objs[p].y - 0x3000;
    if (pl->cam_x < scrollmin_x) pl->cam_x = scrollmin_x;
    if (pl->cam_y < scrollmin_y) pl->cam_y = scrollmin_y;
    if (pl->cam_x > scrollmax_x) pl->cam_x = scrollmax_x;
    if (pl->cam_y > scrollmax_y) pl->cam_y = scrollmax_y;
}

/* The info value under Keen pi as the original reads it: the last non-zero
 * value among the tiles his hitbox touches (and where that tile is). */
static int map_info_under(int pi, int *lx, int *ly)
{
    obj_t *k = &objs[pi];
    calc_hitbox_obj(k);
    int sel = 0;
    for (int tx = k->x1 / 0x1000; tx <= k->x2 / 0x1000; tx++)
        for (int ty = k->y1 / 0x1000; ty <= k->y2 / 0x1000; ty++)
            if (MAPI(tx, ty)) {
                *lx = tx;
                *ly = ty;
                sel = MAPI(tx, ty);
                if (sel == 0xff)
                    sel = 0;
            }
    return sel;
}

/* Does Keen pi touch an entrance tile of level `level`? */
static int map_touches_level(int pi, int level)
{
    obj_t *k = &objs[pi];
    calc_hitbox_obj(k);
    for (int tx = k->x1 / 0x1000; tx <= k->x2 / 0x1000; tx++)
        for (int ty = k->y1 / 0x1000; ty <= k->y2 / 0x1000; ty++)
            if (MAPI(tx, ty) == level)
                return 1;
    return 0;
}

/* Player p pressed jump or pogo on the entrance of `lvl`: make, replace or
 * confirm the start request. Returns the level when everybody has confirmed. */
static int map_press(int p, int lvl)
{
    if (lvl < 1 || lvl > 16 || gs.done[lvl - 1])
        return 0;
    if (game.req_by < 0 || game.req_level != lvl) {
        game.req_by = p;
        game.req_level = lvl;
        game.req_mask = 1 << p;
    } else {
        game.req_mask |= 1 << p;
    }
    if (game.req_mask != game.present)
        return 0;
    game.entry_x = objs[game.req_by].x;
    game.entry_y = objs[game.req_by].y;
    return lvl;
}

/* One frame of a map teleport: the pad animates for 16 frames while the Keen
 * stands still, he moves, the destination pad animates, and he waits for the
 * buttons to be released. The tiles change on the same frames as in the
 * blocking animation of the original. */
static void tele_step(int pi)
{
    player_t *pl = &players[pi];
    obj_t *k = &objs[pi];
    if (pl->tele == TP_WAIT) {
        if (!pl->ctrl.b1 && !pl->ctrl.b2)
            pl->tele = TP_NONE;
        return;
    }
    int base = pl->tele_flag ? 0x156 : 0x152;
    set_tile(pl->tele_tx, pl->tele_ty, (pl->tele_t / 2 + 1) % 4 + base);
    if (++pl->tele_t < 16)
        return;
    set_tile(pl->tele_tx, pl->tele_ty, pl->tele_flag ? 99 : 0x145);
    if (pl->tele == TP_FROM) {
        k->x = pl->map_x = pl->tele_x;
        k->y = pl->map_y = pl->tele_y;
        pl->cam_x = k->x - 0x9000;
        pl->cam_y = k->y - 0x3000;
        pl->tele = TP_TO;
        pl->tele_t = 0;
        pl->tele_tx = k->x / 0x1000;
        pl->tele_ty = k->y / 0x1000;
        pl->tele_flag = pl->tele_dflag;
    } else {
        pl->tele = TP_WAIT;
    }
}

/* Keen pi for one map frame. Returns the level to start, or 0. */
static int map_coop_player(int pi)
{
    obj_t *k = &objs[pi];
    player_t *pl = &players[pi];
    const ctrl_t *c = &pl->ctrl;
    if (pl->tele) {
        k->timer = 0;
        tele_step(pi);
        return 0;
    }
    int lx = 0, ly = 0, sel = 0;
    if (c->b1 || c->b2)
        sel = map_info_under(pi, &lx, &ly);
    calc_hitbox_obj(k);
    k->dy = k->dx = 0;
    switch (c->dir) {
    case 0: k->dy = -0x400; k->sprite = 0x2c; break;
    case 1: k->dy = -0x400; k->dx = 0x400; k->sprite = 0x2c; break;
    case 2: k->dx = 0x400; k->sprite = 0x20; break;
    case 3: k->dy = 0x400; k->dx = 0x400; k->sprite = 0x24; break;
    case 4: k->dy = 0x400; k->sprite = 0x24; break;
    case 5: k->dy = 0x400; k->dx = -0x400; k->sprite = 0x24; break;
    case 6: k->dx = -0x400; k->sprite = 0x28; break;
    case 7: k->dy = -0x400; k->dx = -0x400; k->sprite = 0x2c; break;
    }
    int moving = k->dx || k->dy;
    k->timer = moving ? (gtick >> 4) & 3 : 0;      /* walking animation frame */
    block_mask = 0x8000;                           /* no walking through blocks in co-op */
    int blocked = map_clip(k);
    k->x += k->dx;
    k->y += k->dy;
    if (moving && ((gtick >> 3) & 3) == 0 && pi == local_player)
        snd_play(blocked ? 2 : 1);
    int32_t kx = k->x - pl->cam_x, ky = k->y - pl->cam_y;
    if (k->dx > 0 && kx > 0xb000) {
        pl->cam_x += k->dx;
        if (pl->cam_x > scrollmax_x)
            pl->cam_x = scrollmax_x;
    } else if (k->dx < 0 && kx < 0x9000) {
        pl->cam_x += k->dx;
        if (pl->cam_x < scrollmin_x)
            pl->cam_x = scrollmin_x;
    }
    if (k->dy > 0 && ky > 0x7000) {
        pl->cam_y += k->dy;
        if (pl->cam_y > scrollmax_y)
            pl->cam_y = scrollmax_y;
    } else if (k->dy < 0 && ky < 0x3000) {
        pl->cam_y += k->dy;
        if (pl->cam_y < scrollmin_y)
            pl->cam_y = scrollmin_y;
    }
    if (!sel)
        return 0;
    if (sel >= 1 && sel <= 16)
        return !pl->ctrl_prev.b1 && !pl->ctrl_prev.b2 ? map_press(pi, sel) : 0;
    if (sel == 0x14) {
        ui_open_ship(pi);
    } else if ((sel & 0x20) == 0x20) {
        int dst = (sel & 3) - 1, src = ((sel >> 2) & 3) - 1;
        snd_play(0x12);
        pl->tele = TP_FROM;
        pl->tele_t = 0;
        pl->tele_tx = lx;
        pl->tele_ty = ly;
        pl->tele_flag = ds_w(0x285a + src * 10);
        pl->tele_x = ds_l(0x2852 + dst * 10);
        pl->tele_y = ds_l(0x2856 + dst * 10);
        pl->tele_dflag = ds_w(0x285a + dst * 10);
    }
    return 0;
}

/* One simulated frame of the co-op world map. Returns the level to start, or 0. */
static int map_restart;             /* online: a snapshot arrived, the map phase starts over from it */

static int map_frame(void)
{
    if (net_online && net_frame_begin()) {
        map_restart = 1;
        return 0;
    }
    frame_start();
    if (replaying)
        replay_apply();
    else
        sim_apply_script_warps();
    for (int i = 0; i < nplayers; i++)
        read_player_ctrl(i);
    record_frame();
    coop_events(0);
    int start = 0;
    if (game.pause_mask) {
        gtick -= tics;                  /* the game stands still; only the frame counter runs */
    } else {
        if (game.req_by >= 0 && !map_touches_level(game.req_by, game.req_level))
            game.req_by = -1;           /* the requester walked off the entrance */
        for (int i = 0; i < nplayers; i++) {
            if (!PRESENT(i))
                continue;
            int lvl = map_coop_player(i);
            if (lvl && !start)
                start = lvl;
        }
    }
    if (replaying) {
        /* a replay says when the start request changes, for tests to read */
        static int32_t last_by = -1, last_level, last_mask;
        if (frame_no == 0)
            last_by = -1;
        if (game.req_by != last_by || game.req_level != last_level || game.req_mask != last_mask) {
            if (game.req_by < 0)
                printf("replay: map frame %u: no request\n", frame_no);
            else
                printf("replay: map frame %u: request by player %d for level %d, confirmed mask %d\n",
                       frame_no, game.req_by + 1, game.req_level, game.req_mask);
            last_by = game.req_by;
            last_level = game.req_level;
            last_mask = game.req_mask;
        }
    }
    draw_camera();
    for (int i = 0; i < nplayers; i++)
        if (PRESENT(i))
            draw_keen(objs[i].x, objs[i].y, objs[i].sprite + objs[i].timer, i);
    frame_refresh();
    for (int i = 0; i < nplayers; i++) {
        if (PRESENT(i)) {
            players[i].map_x = objs[i].x;
            players[i].map_y = objs[i].y;
        }
        players[i].ctrl_prev = players[i].ctrl;
    }
    int lead = 0;                       /* the shared record follows the lowest-numbered player */
    while (lead < nplayers - 1 && !PRESENT(lead))
        lead++;
    map_sx = players[lead].cam_x;
    map_sy = players[lead].cam_y;
    gs.mapx = players[lead].map_x;
    gs.mapy = players[lead].map_y;
    gs.mapsx = map_sx;
    gs.mapsy = map_sy;
    frame_no++;
    replay_frame_end();
    return start;
}

/* The co-op map phase. Returns the level to start (sel_level), or 0 when the
 * game is over (quit, or a replay ran out). */
static int coop_map_loop(void)
{
    if (replaying && !replay_next_segment(1, 0))
        return 0;
    record_map_begin();
    coop_map_begin();
    for (int i = 0; i < nplayers; i++) {
        obj_t *k = &objs[i];
        memset(k, 0, sizeof *k);
        if (!PRESENT(i))
            continue;
        k->type = OT_KEEN;
        k->think = think_none;
        k->touch = touch_none;
        k->active = 1;
        k->sprite = 0x24;
        k->x = players[i].map_x;
        k->y = players[i].map_y;
    }
    memset(objs + nplayers, 0, (MAXOBJS - nplayers) * sizeof objs[0]);
    numobjs = nplayers;
    numspecs = 0;
    gtick = 0;
    frame_no = 0;
    rf_hook = ui_overlay_hook;
    map_restart = 0;
    if (net_online) {
        net_segment_begin();
        net_restore_apply();        /* a joiner or a repaired browser continues from a snapshot */
    }
    int chosen = 0;
    for (;;) {
        chosen = map_frame();
        if (map_restart) {
            chosen = -1;
            break;
        }
        if (chosen)
            break;
        if (!replaying) {
            int screen = ui_poll_coop();
            if (screen) {
                /* Help and key setup stop the game for everybody: the pause
                 * events go through the input stream around the local screen. */
                ui_emit(local_player, screen == 0x3f ? EV_SAVE_ON : EV_PAUSE_ON);
                map_frame();
                if (map_restart) {
                    chosen = -1;
                    break;
                }
                if (net_online)
                    net_local_screen(1);    /* the others' frames need our (neutral) input meanwhile */
                ui_run_screen(screen);
                if (net_online)
                    net_local_screen(0);
                ui_emit(local_player, EV_PAUSE_OFF);
                chosen = map_frame();
                if (map_restart) {
                    chosen = -1;
                    break;
                }
                reset_frame_time();
                if (chosen)
                    break;
            }
        } else if (replay_level_done()) {
            break;
        }
        if (abort_game)
            break;
    }
    record_map_end();
    rf_hook = NULL;
    return chosen;
}

/* 0x8B1E: arriving through the teleporter on the dark side of Mars. */
static void level_teleport(void)
{
    load_level(0x50);
    snd_play(0x12);
    int32_t tx = ds_l(0x2866);
    int32_t ty = ds_l(0x286a);
    for (int i = 0; i < nplayers; i++) {
        obj_t *k = &objs[i];
        if (!PRESENT(i))
            continue;
        k->dx = k->dy = k->xv = k->yv = 0;
        k->sprite = 0x24;
        k->x = players[i].map_x = tx;
        k->y = players[i].map_y = ty;
    }
    obj_t *k = &objs[local_player];
    if (game.coop) {
        /* Everybody arrives at once; the pad animation runs as a timed state. */
        for (int i = 0; i < nplayers; i++) {
            if (!PRESENT(i))
                continue;
            player_t *pl = &players[i];
            pl->cam_x = tx - 0x9000;
            pl->cam_y = ty - 0x3000;
            pl->tele = TP_TO;
            pl->tele_t = 0;
            pl->tele_tx = tx / 0x1000;
            pl->tele_ty = ty / 0x1000;
            pl->tele_flag = ds_w(0x286e);
        }
        draw_camera();
        frame_start();
        frame_refresh();
        fade_in();
        return;
    }
    scroll_x = tx - 0x9000;
    scroll_y = ty - 0x3000;
    frame_start();
    frame_refresh();
    fade_in();
    teleport_anim(tx / 0x1000, ty / 0x1000, ds_w(0x286e));
    wait_buttons_up_map(k);
    map_sx = scroll_x;
    map_sy = scroll_y;
}

/* ------------------------------------------------------------------ */
/* 0x839A: the ending */

static void type_text(const char *s)
{
    for (; *s; s++) {
        char c[2] = {*s, 0};
        print(c);
        wait_vbl(6);
    }
}

static void ship_frame(int32_t x, int32_t y, int sprite)
{
    frame_start();
    rf_sprite(x, y, sprite);
    frame_refresh();
}

static void fly_path(unsigned table, int32_t *x, int32_t *y, int sprite, int delay)
{
    for (int i = 0;; i++) {
        int dx = ds_w(table + i * 4), dy = ds_w(table + i * 4 + 2);
        *x += dx * 256;
        *y += dy * 256;
        ship_frame(*x, *y, sprite);
        if (delay)
            wait_vbl(delay);
        if (dx + dy == 0)
            break;
    }
}

static void win_rect_clear(void)
{
    extern void ui_clear_last_window(void);
    ui_clear_last_window();
}

void ending(void)
{
    rf_hook = NULL;
    flush_input();
    load_level(0x50);
    scroll_x = 0x3000;
    scroll_y = 0x23000;
    frame_start();
    rf_sprite(0xd000, 0x27000, 0x28);
    frame_refresh();
    wait_vbl(300);
    win_open(5, 0xf, 0x2a, 0x14);
    fade_in();
    type_text(ds_str(0x2bc7));
    type_text(ds_str(0x2bec));
    type_text(ds_str(0x2c0f));
    type_text(ds_str(0x2c34));
    load_level(0x51);
    wait_vbl(0xf0);
    fade_out();
    scroll_x = scroll_y = 0;
    frame_start();
    frame_refresh();
    frame_refresh();
    fade_in();

    int32_t x = 0x6400, y = 0x5000;
    fly_path(0x28f2, &x, &y, 0x73, 4);
    for (int i = 0; i <= 0x14; i++) {
        x += 0x300;
        y += 0x100;
        ship_frame(x, y, 0x73);
    }
    for (int i = 0; i <= 200; i++) {
        scroll_x += 0x300;
        scroll_y += 0x100;
        x += 0x300;
        y += 0x100;
        ship_frame(x, y, 0x73);
    }
    for (int i = 0; i <= 0x3c; i++) {
        y += 0x100;
        if (i < 0x14)
            scroll_y += 0x300;
        ship_frame(x, y, 0x73);
    }
    wait_vbl(0x14);
    for (int i = 0; i <= 0x1e; i++) {
        frame_start();
        rf_sprite(x, y, 0x73);
        rf_sprite(x + 0x600, y - 0xe00, 0x76);
        frame_refresh();
    }
    for (int i = 0; i <= 0x32; i++) {
        frame_start();
        x -= 0x200;
        rf_sprite(x, y, 0x74);
        frame_refresh();
    }
    for (int i = 0; i <= 0x1e; i++) {
        frame_start();
        rf_sprite(x, y, 0x74);
        rf_sprite(x + 0x600, y - 0xe00, 0x75);
        frame_refresh();
    }
    fly_path(0x29da, &x, &y, 0x73, 0);
    wait_vbl(0x3c);
    fade_out();

    int32_t sx = scroll_x, sy = scroll_y;
    show_pic("FINALE.CK1");
    scroll_x = scroll_y = 0;
    win_open(4, 0xf, 0x1b, 0x13);
    fade_in();
    type_text(ds_str(0x2c60));
    type_text(ds_str(0x2c78));
    type_text(ds_str(0x2c8c));
    wait_vbl(0x78);
    win_rect_clear();
    wait_vbl(0x3c);
    draw_bitmap(0xe, 0, 0xc);
    snd_play(0x19);
    wait_vbl(0x78);
    draw_bitmap(0xe, 0, 0xd);
    snd_play(0x19);
    wait_vbl(300);
    win_open(4, 0xf, 0x1b, 0x13);
    type_text(ds_str(0x2c9e));
    type_text(ds_str(0x2cb3));
    type_text(ds_str(0x2cc8));
    wait_vbl(0xb4);
    win_rect_clear();
    wait_vbl(0x3c);
    draw_bitmap(0xe, 0, 0xc);
    snd_play(0x19);
    win_open(4, 0xf, 0x1b, 0x14);
    type_text(ds_str(0x2cd3));
    print(ds_str(0x2cea));
    print(ds_str(0x2d01));
    print(ds_str(0x2d16));
    wait_vbl(0xf0);
    win_rect_clear();
    win_open(4, 0xf, 0x1b, 0x12);
    type_text(ds_str(0x2d23));
    type_text(ds_str(0x2d35));
    wait_vbl(0xb4);
    win_rect_clear();
    win_open(4, 0xf, 0x1d, 0x13);
    type_text(ds_str(0x2d3f));
    type_text(ds_str(0x2d57));
    type_text(ds_str(0x2d6d));
    wait_vbl(0xb4);
    win_rect_clear();
    win_open(4, 0xf, 0x1b, 0x11);
    type_text(ds_str(0x2d85));
    wait_vbl(0xb4);
    win_open(4, 0xf, 0x1b, 0x11);
    type_text(ds_str(0x2d99));
    wait_vbl(0xb4);
    win_rect_clear();
    wait_vbl(0x3c);
    draw_bitmap(0xe, 0, 0xd);
    snd_play(0x19);
    wait_vbl(300);
    draw_bitmap(0xe, 0, 0xc);
    win_open(4, 0xf, 0x1d, 0x15);
    snd_play(0x19);
    type_text(ds_str(0x2daa));
    type_text(ds_str(0x2dc1));
    type_text(ds_str(0x2dda));
    type_text(ds_str(0x2def));
    type_text(ds_str(0x2e06));
    wait_vbl(300);
    fade_out();

    load_level(0x51);
    scroll_x = sx;
    scroll_y = sy;
    frame_start();
    rf_sprite(x, y, 0x74);
    frame_refresh();
    fade_in();
    wait_vbl(0x1e);
    fly_path(0x292e, &x, &y, 0x74, 0);
    ship_frame(x, y, 0x73);
    wait_vbl(0x3c);
    win_open(0xc, 3, 0x20, 5);
    type_text(ds_str(0x2e15));
    wait_vbl(400);
    scroll_x &= ~0xfff;
    frame_start();
    frame_refresh();
    frame_refresh();
    if (game_version == 131)
        snd_play(0x29);
    text_viewer(txt_end, 0, 0x16);
}

/* ------------------------------------------------------------------ */
/* 0x8CE4: game over / high score entry */

/* Online, the high scores are the server's: copy its table over the local one. */
static void scores_from_server(void)
{
    net_score_t rows[7];
    if (!net_scores(rows))
        return;
    memset(&hs, 0, sizeof hs);
    for (int i = 0; i < 7; i++) {
        hs.score[i] = rows[i].score;
        hs.joystick[i] = rows[i].parts & 1;
        hs.battery[i] = (rows[i].parts >> 1) & 1;
        hs.vacuum[i] = (rows[i].parts >> 2) & 1;
        hs.whiskey[i] = (rows[i].parts >> 3) & 1;
        snprintf(hs.name[i], 13, "%s", rows[i].name);
    }
}

static void game_over(void)
{
    int entry = -1;             /* online: the seat that types the team name */
    if (net_online) {
        entry = net_wait_ended();
        scores_from_server();
    }
    int rank = 0;
    for (int i = 0; i < 7; i++)
        if (gs.score > hs.score[i]) {
            rank = i + 1;
            break;
        }
    if (entry >= 0 && !rank)
        rank = 1;               /* the server asked for a name, so the score qualifies */
    scroll_x &= ~0xfff;
    scroll_y &= ~0xfff;
    frame_start();
    frame_refresh();
    int lx = 0, ly = 0;
    if (rank == 0) {
        cur_x = 7;
        cur_y = 10;
    } else {
        win_grow(0x20, 0xd);
        lx = cur_x;
        ly = cur_y;
    }
    draw_bitmap(cur_x + 9, cur_y * 8, 7);
    snd_play(0x10);
    if (rank) {
        int r = rank - 1;
        margin_x = cur_x = lx + 1;
        cur_y = ly + 4;
        char num[16];
        print(ds_str(0x2e29));
        snprintf(num, sizeof num, "%ld", (long)gs.score);
        print(num);
        cur_x = margin_x + 0xb;
        if (gs.joystick) draw_tile_at(cur_x, (cur_y + 2) * 8 - 4, 0x1c0);
        if (gs.battery) draw_tile_at(cur_x + 2, (cur_y + 2) * 8 - 4, 0x1c1);
        if (gs.vacuum) draw_tile_at(cur_x + 4, (cur_y + 2) * 8 - 4, 0x1c2);
        if (gs.whiskey) draw_tile_at(cur_x + 6, (cur_y + 2) * 8 - 4, 0x1c3);
        cur_x = margin_x;
        cur_y += 4;
        print(ds_str(0x2e39));
        print(ds_str(0x2e52));
        print(ds_str(0x2a87 + r * 7));
        if (net_online && entry != net_seat)
            print(" place!\n\n\n  Please wait...");
        else
            print(ds_str(0x2e60));
        for (int i = 5; i >= r; i--) {
            hs.score[i + 1] = hs.score[i];
            hs.joystick[i + 1] = hs.joystick[i];
            hs.battery[i + 1] = hs.battery[i];
            hs.whiskey[i + 1] = hs.whiskey[i];
            hs.vacuum[i + 1] = hs.vacuum[i];
            hs.unused1[i + 1] = hs.unused1[i];
            hs.unused2[i + 1] = hs.unused2[i];
            memcpy(hs.name[i + 1], hs.name[i], 13);
        }
        flush_input();
        if (net_online && entry != net_seat) {
            /* the others see who is typing, and wait for the table */
            char text[48];
            snprintf(text, sizeof text, "PLAYER %d IS ENTERING YOUR TEAM NAME", entry + 1);
            strip(0, text);
            plat_present();
            net_wait_ended();
            scores_from_server();
            goto entered;
        }
        input_line(hs.name[r], 12);
        if (net_online && !hs.name[r][0])
            snprintf(hs.name[r], 13, "TEAM");      /* the server wants a name */
        hs.score[r] = gs.score;
        hs.joystick[r] = gs.joystick;
        hs.battery[r] = gs.battery;
        hs.vacuum[r] = gs.vacuum;
        hs.whiskey[r] = gs.whiskey;
        hs.unused1[r] = 0;
        hs.unused2[r] = gs.unused2;
        if (net_online) {
            int parts = (gs.joystick ? 1 : 0) | (gs.battery ? 2 : 0) | (gs.vacuum ? 4 : 0) |
                        (gs.whiskey ? 8 : 0);
            net_submit_score(hs.name[r], gs.score, parts);
            net_wait_ended();       /* the table is final */
            scores_from_server();
        } else {
            save_file("SCORES.CK1", &hs, 0xcc);
        }
    }
entered:;
    ctrl_t c;
    do {
        read_ctrl(&c);
        wait_vbl(1);
    } while (c.b1 || c.b2);
    flush_input();
    if (rank == 0)
        for (int i = 0; i < 0x168; i++) {
            wait_vbl(1);
            read_ctrl(&c);
            if (c.b1 || c.b2 || key_peek())
                break;
        }
    fade_out();
    flush_input();
    load_level(0x5a);
    page_scores();
    fade_in();
    int timer = 0x960;
    in_world_map = 0;
    for (;;) {
        frame_start();
        frame_refresh();
        read_ctrl(&c);
        if (check_hotkeys())
            page_scores();
        timer -= tics;
        if (c.b1 || c.b2 || key_peek() || timer < 1)
            break;
    }
    fade_out();
    if (net_online) {
        net_event(3);           /* back to the lobby; the page reloads */
        net_idle_forever();
    }
}

/* 0x7DFB: the world map and the game. */
void game_setup(void)
{
    rf_hook = NULL;
    load_level(0x50);
    if (!game_loaded) {
        for (int ty = 0; ty < map_h; ty++)
            for (int tx = 0; tx < map_w; tx++)
                if (MAPI(tx, ty) == 0xff) {
                    gs.mapx = (int32_t)tx << 12;
                    gs.mapy = (int32_t)ty << 12;
                    ty = map_h;
                    break;
                }
        gs.mapsx = gs.mapx - 0x9000;
        gs.mapsy = gs.mapy - 0x3000;
        gs.score = 0;
    }
    map_sx = gs.mapsx;
    map_sy = gs.mapsy;
    for (int i = 0; i < nplayers; i++) {
        players[i].map_x = gs.mapx;
        players[i].map_y = gs.mapy;
        players[i].cam_x = map_sx;
        players[i].cam_y = map_sy;
        players[i].status = PS_ALIVE;
        players[i].tele = TP_NONE;
    }
    game.levels_started = 0;
    if (!net_online)
        game.present = (1 << nplayers) - 1;
    block_mask = 0x8000;
    last_extra = gs.score / 20000 * 20000;
    abort_game = 0;
    anim_shift = 3;
    /* A co-op game starts from its seed (and not from whatever the title screens
     * drew), so every machine begins the world map in the same state. */
    if (game.coop)
        rng_seed(game_seed);
}

void play_game(void)
{
    int ending_done = 0;        /* online: the ending was played and the server told */
    int result = LR_DIED;
    int recenter = 0;           /* co-op: put every Keen at the entrance and centre the cameras */
    int resume_level = 0;       /* online: the level a snapshot to restore was taken in */
    flush_input();
    game_setup();
    if (net_online) {
        int kind = net_resume(&resume_level);
        if (kind != 2)
            resume_level = 0;
    }
    do {
        for (int i = 0; i < nplayers; i++) {
            obj_t *k = &objs[i];
            if (!PRESENT(i))
                continue;
            k->dx = k->dy = k->xv = k->yv = 0;
            k->sprite = 0x24;
        }
        sel_level = 0;
        load_level(0x50);
        find_levels();
        for (int i = 0; i < 16; i++) {
            int x = level_pos[i + 1].x, y = level_pos[i + 1].y;
            if (!gs.done[i])
                continue;
            if (!level_pos[i + 1].wide) {
                MAPI(x, y) = 0;
                MAPT(x, y) = 0x4d;
            } else {
                MAPI(x, y) = MAPI(x + 1, y) = MAPI(x, y + 1) = MAPI(x + 1, y + 1) = 0;
                MAPT(x, y) = 0x4e;
                MAPT(x + 1, y) = 0x4f;
                MAPT(x, y + 1) = 0x50;
                MAPT(x + 1, y + 1) = 0x51;
            }
        }
        if (recenter) {
            for (int i = 0; i < nplayers; i++) {
                players[i].map_x = game.entry_x;
                players[i].map_y = game.entry_y;
            }
        }
        for (int i = 0; i < nplayers; i++) {
            if (!PRESENT(i))
                continue;
            objs[i].x = players[i].map_x;
            objs[i].y = players[i].map_y;
            if (recenter)
                map_camera_center(i);
        }
        recenter = 0;
        if (replaying) {
            printf("replay: world map, entrances:");
            for (int i = 1; i <= 16; i++)
                if (level_pos[i].x && !gs.done[i - 1])
                    printf(" %d@%d,%d", i, level_pos[i].x, level_pos[i].y);
            printf("\n");
            printf("replay: world map, specials (info value @ tile):");
            for (int ty = 0; ty < map_h; ty++)
                for (int tx = 0; tx < map_w; tx++) {
                    int v = MAPI(tx, ty);
                    if (v == 0x14 || (v < 0x8000 && (v & 0x20) == 0x20))
                        printf(" 0x%x@%d,%d", v, tx, ty);
                }
            printf("\n");
        }
        plat_wide(1);                       /* the world map is a wide view, like a level */
        view_map_cam = !game.coop;          /* single player: scroll_x is the camera, placed when drawing */
        if (!game.coop) {
            scroll_x = map_sx;
            scroll_y = map_sy;
        } else {
            draw_camera();
        }
        if (resume_level) {
            sel_level = resume_level;       /* online: straight back into the level of the snapshot */
        } else if (!warp_level) {
            frame_start();
            frame_refresh();
            if (result == LR_DIED && !game.coop)
                keens_left();
            if (result < 2 || (game.coop && result != LR_TELEPORT))
                fade_in();
            if (result == LR_DIED && !game.coop) {
                flush_input();
                ctrl_t c;
                for (int i = 0; i < 0x3c; i++) {
                    wait_vbl(1);
                    read_ctrl(&c);
                    if (c.b1 || c.b2 || key_peek())
                        break;
                }
                do {
                    read_ctrl(&c);
                    wait_vbl(1);
                } while (c.b1 || c.b2);
            }
            flush_input();
            in_world_map = 1;
            reset_frame_time();
            if (game.coop) {
                sel_level = coop_map_loop();
                if (sel_level < 0) {
                    /* online: a snapshot arrived; start the segment it belongs to from it */
                    in_world_map = 0;
                    int kind = net_resume(&resume_level);
                    if (kind != 2)
                        resume_level = 0;
                    continue;
                }
                if (!sel_level)
                    goto out;       /* quit, or a replay ran out */
            } else {
                if (replaying && !replay_next_segment(1, 0))
                    goto out;
                record_map_begin();
                frame_no = 0;
                do {
                    frame_start();
                    if (replaying)
                        replay_apply();
                    for (int i = 0; i < nplayers; i++) {
                        read_player_ctrl(i);
                        map_keen(i);
                    }
                    record_frame();
                    frame_refresh();
                    frame_no++;
                    replay_frame_end();
                    check_cheats();
                    for (int i = 0; i < nplayers; i++) {
                        players[i].map_x = objs[i].x;
                        players[i].map_y = objs[i].y;
                    }
                    map_sx = scroll_x;
                    map_sy = scroll_y;
                    gs.mapx = players[0].map_x;
                    gs.mapy = players[0].map_y;
                    gs.mapsx = map_sx;
                    gs.mapsy = map_sy;
                    if (check_hotkeys())
                        reset_frame_time();
                    if (abort_game)
                        goto out;
                    if (sel_level == 0 && replaying && replay_level_done())
                        goto out;
                } while (sel_level == 0);
                record_map_end();
            }
            in_world_map = 0;
        } else {
            sel_level = warp_level;
        }
        if (resume_level || (sel_level >= 1 && sel_level <= 16 && !gs.done[sel_level - 1])) {
            snd_play(3);
            resume_level = 0;
            do {
                result = play_level(sel_level & 0xff);
                if (replaying)
                    replay_report(sel_level, result);
            } while (game.coop && result == LR_FAIL && !abort_game);   /* restarts at once */
            if (result == LR_RESTORE) {
                /* online: a snapshot arrived mid-level; carry on from it, in whatever it is */
                int kind = net_resume(&resume_level);
                if (kind != 2)
                    resume_level = 0;
                continue;
            }
            if (abort_game) {
                abort_game = 0;
                goto out;
            }
            if (result == LR_STOPPED)
                goto out;
            if (result == LR_DIED) {
                gs.lives--;
            } else {
                if (warp_level) {
                    if (replaying)
                        goto out;
                    plat_shutdown();
                    exit(0);
                }
                if (result == LR_DONE)
                    gs.done[sel_level - 1] = 1;
                else if (result == LR_TELEPORT)
                    level_teleport();
                if (game.coop)
                    recenter = result != LR_TELEPORT;
                if (result != LR_VOTE_LEAVE && gs.joystick && gs.battery && gs.vacuum && gs.whiskey) {
                    if (replaying) {
                        printf("replay: all four ship parts: the ending plays\n");
                    } else {
                        net_event(1);   /* online: tell the server the ending started */
                        plat_wide(0);   /* the ending is made of fixed screens */
                        view_map_cam = 0;
                        if (!plat_headless())
                            ending();   /* for every player in the game (it waits for keys) */
                        net_event(2);
                        ending_done = 1;
                    }
                    goto out;
                }
            }
        }
    } while (game.coop || gs.lives >= 0);
    snd_wait();
    fade_in();
out:
    plat_wide(0);                       /* the rest are fixed screens */
    view_map_cam = 0;
    if (net_online && !ending_done)
        net_idle_forever();     /* the page goes back to the lobby when the server says so */
    if (!replaying)
        game_over();
}
