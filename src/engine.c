/* Object engine, Keen and the level loop (KEEN1.EXE 0x0239..0x5074). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"
#include "gfx.h"
#include "kdata.h"
#include "platform.h"

gamestate_t gs;
player_t players[MAXPLAYERS];
int nplayers = 1;
int local_player;
player_t *cur_plr = &players[0];
uint32_t gtick;
uint32_t frame_no;
int tics;
static uint32_t lasttick;
obj_t objs[MAXOBJS], cur;
int numobjs;
spec_t specs[MAXSPECS];
int numspecs;
int cur_level;
int32_t scrollmin_x, scrollmin_y, scrollmax_x, scrollmax_y;
int32_t objmax_x, objmax_y, keenmax_x, keendeath_y;
int lights_on = 1;
int level_result, godmode, abort_game, warp_level;
int32_t last_extra;
int in_world_map;

/* The Keen being thought for (set by play_level before each Keen's think). */
#define plr cur_plr

/* Control bindings from CTLPANEL.CK1: 8 direction keys, 2 buttons. */
uint8_t key_dir[8] = {0x48, 0x49, 0x4d, 0x51, 0x50, 0x4f, 0x4b, 0x47};
uint8_t key_b1 = 0x1d, key_b2 = 0x38;
/* Player 2's keys (local two-player mode): Q W E / A D / Z S C around the
 * home row, V jumps, B pogos, so both players fit on one keyboard. */
const uint8_t key2_dir[8] = {0x11, 0x12, 0x20, 0x2e, 0x1f, 0x2c, 0x1e, 0x10};
const uint8_t key2_b1 = 0x2f, key2_b2 = 0x30;

/* ------------------------------------------------------------------ */
/* Random numbers: a 256 byte table (DS:4F86) and an additive generator
 * seeded from DS:50AE. Both are seeded from the clock like the original. */

int rnd_index;
uint16_t rng_state[18];
int rng_a = 0x22, rng_b = 0x0a;
uint16_t rng_sum;

int rand8(void)
{
    rnd_index = (rnd_index + 1) & 0xff;
    return ds_b(0x4f86 + rnd_index);
}

int rand_range(int max)
{
    uint16_t mask = 0xffff, p = (uint16_t)max;
    for (;;) {
        int top = (p & 0x8000) != 0;
        p <<= 1;
        if (top)
            break;
        mask >>= 1;
    }
    uint16_t v = rng_state[rng_a / 2] + rng_state[rng_b / 2] + 1;
    rng_state[rng_a / 2] = v;
    rng_sum += v;
    rng_a -= 2;
    if (rng_a == 0)
        rng_a = 0x22;
    rng_b -= 2;
    if (rng_b == 0)
        rng_b = 0x22;
    uint16_t r = rng_sum & mask;
    if ((uint16_t)max < r)
        r >>= 1;
    return r;
}

void rng_init(void)
{
    rng_seed((uint32_t)time(NULL));
}

/* All game randomness comes from this state (rand8, rand_range); it is part
 * of every snapshot. The same seed gives the same sequence on any machine. */
void rng_seed(uint32_t t)
{
    for (int i = 0; i < 17; i++)
        rng_state[i + 1] = (uint16_t)ds_w(0x50ae + i * 2);
    rng_sum = 0;
    rng_a = 0x22;
    rng_b = 0x0a;
    rng_state[5] = (uint16_t)t;
    rng_state[17] = (uint16_t)(t >> 3);
    rand_range(0xffff);
    rnd_index = (int)(t % 100);
}

/* ------------------------------------------------------------------ */
/* Frame pacing (0x0239) and refresh (0x0289). */

/* The simulation always advances SIM_TICS ticks per frame. Real time only
 * paces drawing: a slow machine slows the game down instead of taking bigger
 * steps, so every machine sees the same game. */
void frame_start(void)
{
    rf_begin();
    if (plat_fast) {
        /* online catch-up: no pacing, the game just runs */
    } else if (plat_headless()) {
        /* Headless replay: no pacing, optionally a fixed delay to show that
         * results do not depend on real time. */
        if (headless_delay_ms > 0)
            plat_delay(headless_delay_ms);
    } else {
        for (;;) {
            /* pacing only: wall-clock time never reaches the game state */
            uint32_t now = plat_ticks();
            if (now - lasttick >= SIM_TICS) {
                lasttick = now;
                break;
            }
            plat_pump();
            plat_delay(1);
        }
    }
    tics = SIM_TICS;
    gtick += SIM_TICS;
}

void frame_refresh(void)
{
    rf_refresh();
}

/* Reset frame timing after a pause (menus, windows). */
void reset_frame_time(void)
{
    lasttick = plat_ticks();    /* pacing only */
}

/* Player 1's keys: W A S D also move, unless a second player needs them. */
static int wasd_alias(void)
{
    return nplayers < 2 || net_online;
}

static void read_keys(ctrl_t *c, const uint8_t *dir, int b1, int b2)
{
    plat_pump();
    int y = 0, x = 0;
    if (keydown[dir[0]]) y = -1;
    if (keydown[dir[2]]) x = 1;
    if (keydown[dir[4]]) y = 1;
    if (keydown[dir[6]]) x = -1;
    if (keydown[dir[1]]) { y = -1; x = 1; }
    if (keydown[dir[7]]) { y = -1; x = -1; }
    if (keydown[dir[3]]) { y = 1; x = 1; }
    if (keydown[dir[5]]) { y = 1; x = -1; }
    if (dir == key_dir && wasd_alias()) {
        if (keydown[0x11]) y = -1;      /* W */
        if (keydown[0x20]) x = 1;       /* D */
        if (keydown[0x1f]) y = 1;       /* S */
        if (keydown[0x1e]) x = -1;      /* A */
    }
    static const int dirs[9] = {7, 0, 1, 6, 8, 2, 5, 4, 3};
    c->dir = dirs[y * 3 + x + 4];
    c->b1 = keydown[b1] != 0;
    c->b2 = keydown[b2] != 0;
}

/* Menus and windows read key set 1. */
void read_ctrl(ctrl_t *c)
{
    read_keys(c, key_dir, key_b1, key_b2);
}

/* In a level or on the map: the input of player p for this frame, from that
 * player's input source. A replay (or later the network) has set it already. */
void read_player_ctrl(int p)
{
    switch (players[p].input) {
    case IN_KEYS1:
        read_keys(&players[p].ctrl, key_dir, key_b1, key_b2);
        players[p].ctrl.ev = 0;
        break;
    case IN_KEYS2:
        read_keys(&players[p].ctrl, key2_dir, key2_b1, key2_b2);
        players[p].ctrl.ev = 0;
        break;
    case IN_REPLAY:
    case IN_NET:
        break;
    }
    /* Local overlays and votes shape a keyboard player's input (ui.c). */
    if (game.coop && (players[p].input == IN_KEYS1 || players[p].input == IN_KEYS2))
        ui_filter_ctrl(p, &players[p].ctrl);
}

/* ------------------------------------------------------------------ */
/* Map access */

int tile_at(int tx, int ty)
{
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx >= map_w) tx = map_w - 1;
    if (ty >= map_h) ty = map_h - 1;
    return MAPT(tx, ty);
}

void set_tile(int tx, int ty, int t)
{
    if (tx >= 0 && ty >= 0 && tx < map_w && ty < map_h)
        MAPT(tx, ty) = (uint16_t)t;
}

static int info_at(int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= map_w || ty >= map_h)
        return 0;
    return MAPI(tx, ty);
}

/* 0x066B: load LEVELnn and compute the scroll / object limits. */
void load_level(int num)
{
    cur_level = num;
    if (!load_level_file(num)) {
        fprintf(stderr, "Missing LEVEL%02d.CK1\n", num);
        exit(1);
    }
    scrollmin_x = 0x2000;
    scrollmin_y = 0x2000;
    scrollmax_x = (int32_t)(map_w - 22) << 12;
    objmax_x = (int32_t)(map_w - 2) << 12;
    objmax_y = (int32_t)map_h << 12;
    scrollmax_y = ((int32_t)(map_h - 15) << 12) + 0x800;
    keenmax_x = (int32_t)(map_w - 3) << 12;
    keendeath_y = (int32_t)map_h << 12;
}

/* ------------------------------------------------------------------ */
/* Objects */

void think_none(void) {}
void touch_none(obj_t *self, obj_t *other) { (void)self; (void)other; }
void spec_none(spec_t *s) { (void)s; }

obj_t *new_obj(void)
{
    int i = nplayers;        /* slots 0..nplayers-1 belong to the Keens */
    while (i < numobjs && objs[i].type)
        i++;
    if (i >= MAXOBJS)
        i = MAXOBJS - 1;
    if (i >= numobjs)
        numobjs = i + 1;
    obj_t *o = &objs[i];
    memset(o, 0, sizeof *o);
    o->think = think_none;
    o->touch = touch_none;
    o->active = 1;
    return o;
}

spec_t *new_spec(void)
{
    int i = 0;
    while (i < numspecs && specs[i].active)
        i++;
    if (i >= MAXSPECS)
        i = MAXSPECS - 1;
    if (i >= numspecs)
        numspecs = i + 1;
    spec_t *s = &specs[i];
    memset(s, 0, sizeof *s);
    s->think = spec_none;
    return s;
}

static int touching(const obj_t *a, const obj_t *b)
{
    if (a->x1 == 0 || b->x1 == 0)
        return 0;
    return b->x1 <= a->x2 && b->y1 <= a->y2 && a->x1 <= b->x2 && a->y1 <= b->y2;
}

void calc_hitbox_obj(obj_t *o)
{
    const sprite_t *s = &sprites[o->sprite >= 0 && o->sprite < NUMSPRITES ? o->sprite : 0];
    o->x1 = o->x + s->hb[0];
    o->x2 = o->x + s->hb[2];
    o->y1 = o->y + s->hb[1];
    o->y2 = o->y + s->hb[3];
}

void calc_hitbox(void)
{
    calc_hitbox_obj(&cur);
}

/* 0x2C05 */
void accel_x(int a)
{
    for (int i = 1; i <= tics; i++) {
        cur.xv += a;
        if (cur.xv > 120)
            cur.xv = 120;
        else if (cur.xv < -120)
            cur.xv = -120;
        if (i != tics)
            cur.dx += cur.xv;
    }
}

/* 0x2C4A */
static void accel_y_limit(int max, int a)
{
    for (int i = 1; i <= tics; i++) {
        cur.yv += a;
        if (cur.yv > max)
            cur.yv = max;
        else if (cur.yv < -max)
            cur.yv = -max;
        if (i != tics)
            cur.dy += cur.yv;
    }
}

/* 0x2C95 */
void gravity(void)
{
    for (int i = 1; i <= tics; i++) {
        cur.yv += 3;
        if (cur.yv > 200)
            cur.yv = 200;
        else if (cur.yv < -400)
            cur.yv = -400;
        if (i != tics)
            cur.dy += cur.yv;
    }
}

/* 0x2D02: move the hitbox by dx/dy against the tile edges.
 * Returns 1 = blocked left, 2 = landed, 4 = blocked right, 8 = hit head. */
int clip_move(void)
{
    int flags = 0;
    if (cur.dx > 0xf00) cur.dx = 0xf00;
    else if (cur.dx < -0xf00) cur.dx = -0xf00;
    if (cur.dy > 0xf00) cur.dy = 0xf00;
    else if (cur.dy < -0xf00) cur.dy = -0xf00;

    cur.y2 += cur.dy;
    cur.y1 += cur.dy;
    int tx1 = cur.x1 >> 12, tx2 = cur.x2 >> 12;
    if (cur.dy > 0) {
        int ty = cur.y2 >> 12;
        if (ty != (cur.y2 - cur.dy) >> 12)
            for (int tx = tx1; tx <= tx2; tx++)
                if (T_TOP(tile_at(tx, ty))) {
                    cur.yv = 0;
                    int m = (int)((cur.y2 + 1) % 0x1000);
                    cur.dy -= m;
                    cur.y1 -= m;
                    cur.y2 -= m;
                    flags = 2;
                    break;
                }
    } else if (cur.dy < 0) {
        int ty = cur.y1 >> 12;
        if (ty != (cur.y1 - cur.dy) >> 12)
            for (int tx = tx1; tx <= tx2; tx++)
                if (T_BOTTOM(tile_at(tx, ty))) {
                    cur.yv = 0;
                    int m = 0x1000 - (int)(cur.y1 % 0x1000);
                    cur.dy += m;
                    cur.y1 += m;
                    cur.y2 += m;
                    flags = 8;
                    break;
                }
    }

    cur.x1 += cur.dx;
    cur.x2 += cur.dx;
    int ty1 = cur.y1 >> 12, ty2 = cur.y2 >> 12;
    if (cur.dx > 0) {
        int tx = cur.x2 >> 12;
        if (tx != (cur.x2 - cur.dx) >> 12)
            for (int ty = ty1; ty <= ty2; ty++)
                if (T_LEFT(tile_at(tx, ty))) {
                    cur.xv = 0;
                    int m = (int)((cur.x2 + 1) % 0x1000);
                    cur.dx -= m;
                    cur.x1 -= m;
                    cur.x2 -= m;
                    return flags | 4;
                }
    } else if (cur.dx < 0) {
        int tx = cur.x1 >> 12;
        if (tx != (cur.x1 - cur.dx) >> 12)
            for (int ty = ty1; ty <= ty2; ty++)
                if (T_RIGHT(tile_at(tx, ty))) {
                    cur.xv = 0;
                    int m = 0x1000 - (int)(cur.x1 % 0x1000);
                    cur.dx += m;
                    cur.x1 += m;
                    cur.x2 += m;
                    return flags | 1;
                }
    }
    return flags;
}

/* 0x2CD8 */
int move_obj(void)
{
    cur.dx += cur.xv * tics;
    cur.dy += cur.yv * tics;
    return clip_move();
}

/* 0x3301: limits for objects other than Keen; returns 1 to skip thinking. */
static int obj_offscreen(void)
{
    if (game_version == 131) {
        /* v1.31 removes objects that leave the level instead of clamping. */
        if (cur.y < 0)
            cur.y = 0;
        if (cur.x > objmax_x || cur.x < 0 || cur.y > objmax_y) {
            cur.type = 0;
            return 1;
        }
    } else {
        if (cur.x > objmax_x)
            cur.x = objmax_x;
        else if (cur.x < 0)
            cur.x = 0;
        if (cur.y < 0)
            cur.y = 0;
        else if (cur.y > objmax_y) {
            cur.type = 0;
            return 1;
        }
    }
    int tx = cur.x >> 12, ty = cur.y >> 12;
    /* Stays awake while any player's camera still sees it. */
    if (!cam_sees(tx, ty, 9, 9, 0x1d, 0x13)) {
        if (cur.type < 9)
            cur.active = 0;
        else
            cur.type = 0;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Score, death */

void add_score(int pts)
{
    gs.score += pts;
    if (!game.coop && gs.score - last_extra >= 20000) {
        snd_play(0x1c);
        last_extra = gs.score / 20000 * 20000;
        gs.lives++;
    }
}

/* Players and cameras */

static int keen_living(const obj_t *o)
{
    extern void keen_exit_walk(void);
    extern void keen_dying(void);
    return o->type == OT_KEEN && o->think != keen_dying && o->think != keen_exit_walk;
}

/* Player number of a Keen object (slot index), or -1 for anything else. */
int player_of(const obj_t *o)
{
    int i = (int)(o - objs);
    return i >= 0 && i < nplayers ? i : -1;
}

/* Nearest living Keen to `cur`, measured horizontally; ties go to the lower
 * player number. With nobody alive it is Keen 0, as in the original. */
obj_t *nearest_keen(void)
{
    obj_t *best = NULL;
    int32_t bd = 0;
    for (int i = 0; i < nplayers; i++) {
        if (!keen_living(&objs[i]))
            continue;
        int32_t d = objs[i].x > cur.x ? objs[i].x - cur.x : cur.x - objs[i].x;
        if (!best || d < bd) {
            best = &objs[i];
            bd = d;
        }
    }
    return best ? best : &objs[0];
}

/* True if the tile (tx, ty) lies inside the window around any camera of a
 * player still in the level: cam_tx - l < tx < cam_tx + r, same for y. */
int cam_sees(int tx, int ty, int l, int t, int r, int b)
{
    for (int i = 0; i < nplayers; i++) {
        if (objs[i].type != OT_KEEN)
            continue;
        const player_t *p = &players[i];
        if (p->cam_tx - l < tx && p->cam_ty - t < ty && tx < p->cam_tx + r && ty < p->cam_ty + b)
            return 1;
    }
    return 0;
}

/* 0x42E9: works on the stored Keen object (called outside his think). */
void kill_keen(obj_t *k)
{
    extern void keen_dying(void);
    int p = player_of(k);
    if (godmode || (p >= 0 && players[p].invuln))
        return;
    k->think = keen_dying;
    k->touch = touch_none;
    k->y += 0x800;
    k->yv = 0;
    k->xv = 0;
    k->timer = 0;
    k->sprite = 0x16;
    snd_play_always(8);                 /* a partner's death is heard wherever he is */
}

/* 0x4335: Keen fell out of the level (inside his think). */
static void keen_fell(void)
{
    extern void keen_dying(void);
    cur.think = keen_dying;
    cur.touch = touch_none;
    cur.y += 0x800;
    cur.xv = 0;
    objs[plr - players].yv = 0;
    objs[plr - players].xv = 0;
    cur.timer = 0;
    cur.sprite = 0x16;
    snd_play_always(8);
}

/* 0x33EE: keep Keen inside the level, kill him below it. */
static void keen_bounds(void)
{
    int32_t minx = scrollmin_x + 8;
    if (cur.x < minx) {
        cur.xv = 0;
        cur.dx = 0;
        cur.x = minx;
    } else if (cur.x > keenmax_x) {
        cur.xv = 0;
        cur.dx = 0;
        cur.x = keenmax_x;
    }
    if (cur.y < scrollmin_y) {
        cur.dy = 0;
        cur.yv = 0;
        cur.y = scrollmin_y;
    } else if (cur.y > keendeath_y) {
        snd_play_always(0x1b);
        snd_wait();
        keen_fell();
    }
}

/* 0x34A1: scroll to follow Keen i. Each player has a camera of his own. */
extern void keen_exit_walk(void);
extern void keen_dying(void);

static void scroll_follow(int i)
{
    obj_t *k = &objs[i];
    player_t *p = &players[i];
    if (k->think == keen_exit_walk || k->think == keen_dying)
        return;
    int32_t kx = k->x - p->cam_x, ky = k->y - p->cam_y;
    if (k->dx > 0 && kx > 0xb000) {
        p->cam_x += k->dx;
        if (p->cam_x > scrollmax_x)
            p->cam_x = scrollmax_x;
    } else if (k->dx < 0 && kx < 0x9000) {
        p->cam_x += k->dx;
        if (p->cam_x < scrollmin_x)
            p->cam_x = scrollmin_x;
    }
    if (k->dy > 0 && ky > 0x7000) {
        p->cam_y += k->dy;
        if (p->cam_y > scrollmax_y)
            p->cam_y = scrollmax_y;
        return;
    }
    if (k->dy < 0 && ky < 0x3000) {
        p->cam_y += k->dy;
        if (p->cam_y < scrollmin_y)
            p->cam_y = scrollmin_y;
    }
}

/* ------------------------------------------------------------------ */
/* Keen */

void keen_jump_charge(void);
void keen_air(void);
void keen_shoot(void);
void keen_pogo_air(void);
void keen_pogo_charge(void);
void keen_frozen(void);
static void switch_activate(void);
static void spawn_keenshot(int32_t x, int32_t y);

static void start_jump(void)
{
    cur.w2e = cur.xv;
    cur.xv = 0;
    cur.w2c = 0;
    cur.timer = 0;
    cur.w26 = plr->facing >= 0 ? 8 : 0xe;
    cur.think = keen_jump_charge;
}

static void start_shoot(void)
{
    cur.think = keen_shoot;
    cur.dx = 0;
    cur.w2c = 0;
    cur.timer = 0;
    cur.sprite = plr->facing > 0 ? 0x14 : 0x15;
}

/* 0x38F0: standing / walking */
void keen_walk(void)
{
    int slip = 1;
    if (cur.w30 == 0) {
        int tx1 = cur.x1 >> 12, tx2 = cur.x2 >> 12, ty = (cur.y2 >> 12) + 1;
        for (int tx = tx1; tx <= tx2; tx++) {
            int v = T_TOP(tile_at(tx, ty));
            if (v > 1)
                slip = v;
        }
    }
    if (plr->ctrl.b1)
        start_jump();
    if (slip < 3) {
        if (plr->ctrl.dir >= 1 && plr->ctrl.dir <= 3) {
            accel_x(2);
            if (cur.xv < 0)
                plr->ctrl.dir = 8;
        } else if (plr->ctrl.dir >= 5 && plr->ctrl.dir <= 7) {
            accel_x(-2);
            if (cur.xv > 0)
                plr->ctrl.dir = 8;
        }
    }
    if (slip == 1 && plr->ctrl.dir == 8) {
        if (cur.xv > 0) {
            accel_x(-3);
            if (cur.xv < 0)
                cur.xv = 0;
        } else if (cur.xv < 0) {
            accel_x(3);
            if (cur.xv > 0)
                cur.xv = 0;
        }
    }
    if (slip == 3) {
        if (plr->facing > 0)
            cur.xv = 0xb4;
        else if (plr->facing < 0)
            cur.xv = -0xb4;
    }
    uint32_t tick = gtick;
    if (cur.xv == 0) {
        cur.sprite = plr->facing >= 0 ? 0 : 4;
    } else {
        cur.sprite = cur.xv > 0 ? 0 : 4;
        if (slip < 3)
            cur.sprite += (tick >> 4) & 3;
        plr->facing = cur.xv;
    }
    if (cur.xv != 0 && ((tick >> 4) & 1) && slip < 3)
        snd_play(((tick >> 5) & 1) ? 0x1e : 4);
    gravity();
    int f = move_obj();
    keen_bounds();
    if ((f & 5) && ((tick >> 4) & 1))
        snd_play(5);
    if (!(f & 2) && cur.w30 == 0) {
        cur.think = keen_air;
        snd_play(0x1b);
        return;
    }
    if (plr->ctrl.b1)
        start_jump();
    if (plr->ctrl.b2 && !plr->ctrl_prev.b2) {
        if (plr->on_switch) {
            switch_activate();
        } else {
            cur.timer = 0;
            cur.w2c = cur.xv;
            cur.xv = 0;
            if (gs.pogo)
                cur.think = keen_pogo_charge;
        }
    }
    if (plr->ctrl.b1 && plr->ctrl.b2 && !plr->ctrl_prev.b1 && !plr->ctrl_prev.b2)
        start_shoot();
    if (cur.w30)
        cur.w30--;
}

/* 0x3C14: crouching before a jump */
void keen_jump_charge(void)
{
    cur.sprite = cur.w26 + cur.timer / 6;
    if (plr->ctrl.b1)
        cur.w2c += tics * 6;
    else if (cur.timer < 12)
        cur.timer = 24 - cur.timer;
    if (plr->ctrl.dir >= 1 && plr->ctrl.dir <= 3) {
        cur.w2e += tics * 2;
        if (cur.w2e > 120)
            cur.w2e = 120;
    } else if (plr->ctrl.dir >= 5 && plr->ctrl.dir <= 7) {
        cur.w2e -= tics * 2;
        if (cur.w2e < -120)
            cur.w2e = -120;
    }
    cur.xv = 0;
    cur.timer += tics;
    if (cur.timer >= 0x24) {
        cur.think = keen_air;
        cur.yv -= cur.w2c;
        cur.xv = cur.w2e;
        snd_play(6);
    }
    gravity();
    move_obj();
    keen_bounds();
    if (plr->ctrl.b1 && plr->ctrl.b2)
        start_shoot();
}

/* 0x3D22: in the air */
void keen_air(void)
{
    if (plr->ctrl.dir >= 1 && plr->ctrl.dir <= 3) {
        accel_x(2);
        if (cur.xv < 0)
            plr->ctrl.dir = 8;
    } else if (plr->ctrl.dir >= 5 && plr->ctrl.dir <= 7) {
        accel_x(-2);
        if (cur.xv > 0)
            plr->ctrl.dir = 8;
    }
    if (plr->ctrl.dir == 8) {
        if (cur.xv > 0) {
            accel_x(-1);
            if (cur.xv < 0)
                cur.xv = 0;
        } else if (cur.xv < 0) {
            accel_x(1);
            if (cur.xv > 0)
                cur.xv = 0;
        }
    }
    cur.sprite = plr->facing > 0 ? 0xd : 0x13;
    if (cur.xv != 0)
        plr->facing = cur.xv;
    gravity();
    int f = move_obj();
    uint32_t tick = gtick;
    if ((f & 5) && ((tick >> 4) & 1))
        snd_play(5);
    if (f & 2) {
        cur.think = keen_walk;
        snd_play(7);
        return;
    }
    if (f & 8)
        snd_play(0x15);
    keen_bounds();
    if (plr->ctrl.b2 && !plr->ctrl_prev.b2) {
        if (plr->on_switch)
            switch_activate();
        else if (gs.pogo)
            cur.think = keen_pogo_air;
    }
    if (plr->ctrl.b1 && plr->ctrl.b2 && !plr->ctrl_prev.b1 && !plr->ctrl_prev.b2)
        start_shoot();
}

/* 0x3E9B: firing the raygun */
void keen_shoot(void)
{
    cur.timer += tics;
    if (cur.w2c == 0 && cur.timer > 1) {
        if (gs.ammo) {
            snd_play(0xc);
            gs.ammo--;
            spawn_keenshot(cur.x, cur.y);
        } else {
            snd_play(0x24);
        }
        cur.w2c = 1;
    }
    if (cur.timer > 0x1e && !plr->ctrl.b1 && !plr->ctrl.b2)
        cur.think = keen_walk;
    if (cur.xv > 0) {
        accel_x(-1);
        if (cur.xv < 0)
            cur.xv = 0;
    } else if (cur.xv < 0) {
        accel_x(1);
        if (cur.xv > 0)
            cur.xv = 0;
    }
    gravity();
    move_obj();
    keen_bounds();
}

/* 0x3F51: bouncing on the pogo stick */
void keen_pogo_air(void)
{
    /* The original re-polls the keys here; the frame's input is fixed at the
     * start of the frame instead, so every machine sees the same value. */
    if (plr->ctrl.dir >= 1 && plr->ctrl.dir <= 3) {
        accel_x(1);
        if (cur.xv < 0)
            plr->ctrl.dir = 8;
    } else if (plr->ctrl.dir >= 5 && plr->ctrl.dir <= 7) {
        accel_x(-1);
        if (cur.xv > 0)
            plr->ctrl.dir = 8;
    }
    if (plr->ctrl.b1 && cur.yv < 0)
        accel_y_limit(200, -1);
    if (godmode && plr->ctrl.b1)
        cur.yv = -200;
    if (cur.xv != 0)
        plr->facing = cur.xv;
    cur.w26 = plr->facing >= 0 ? 0x18 : 0x1a;
    cur.sprite = cur.w26;
    gravity();
    int f = move_obj();
    uint32_t tick = gtick;
    if ((f & 5) && ((tick >> 4) & 1))
        snd_play(5);
    if (f & 2) {
        cur.think = keen_pogo_charge;
        cur.timer = 0;
        cur.w2c = cur.xv;
        cur.xv = 0;
    }
    if (f & 8)
        snd_play(0x15);
    keen_bounds();
    if (plr->ctrl.b2 && !plr->ctrl_prev.b2) {
        if (plr->on_switch)
            switch_activate();
        else
            cur.think = keen_air;
    }
    if (plr->ctrl.b1 && plr->ctrl.b2)
        start_shoot();
    if (cur.w30)
        cur.w30--;
}

/* 0x40DD: compressing the pogo stick on the ground */
void keen_pogo_charge(void)
{
    cur.xv = 0;
    cur.dx = 0;
    cur.sprite = cur.w26 + 1;
    cur.timer += tics;
    if (cur.timer > 0x16) {
        cur.think = keen_pogo_air;
        cur.w30 = 0;
        cur.yv -= 200;
        cur.xv = cur.w2c;
        snd_play(6);
    }
    if (plr->ctrl.b2 && !plr->ctrl_prev.b2)
        cur.think = keen_walk;
    if (plr->ctrl.b1 && plr->ctrl.b2)
        start_shoot();
    move_obj();
    cur.w30 = 0;
}

/* 0x2769: frozen by an ice block */
void keen_frozen(void)
{
    cur.sprite = 0x1c + ((gtick >> 5) & 1);
    cur.timer -= tics;
    if (cur.timer < 0) {
        cur.sprite = 0x1f;
        if (cur.timer < -50)
            cur.think = keen_walk;
    }
    gravity();
    move_obj();
    keen_bounds();
}

/* 0x4178: walking into the exit door. timer/w2c hold the door tile. */
void keen_exit_walk(void)
{
    int32_t tx = cur.timer, ty = cur.w2c;
    cur.dx = tics * 0x3c;
    cur.sprite = (gtick >> 4) & 3;
    plr->facing = cur.xv;
    rf_tile(tx << 12, ty << 12, 0xa0);
    rf_tile(tx << 12, (ty + 1) << 12, 0xa0);
    rf_tile((tx + 1) << 12, ty << 12, 0x8f);
    rf_tile((tx + 1) << 12, (ty + 1) << 12, 0x8f);
    if ((tx << 12) <= cur.x) {
        cur.type = 0;
        if (game.coop)
            plr->status = PS_EXITED;        /* the level goes on for the others */
        else
            level_result = 1;
    }
}

/* Co-op: a Keen whose death animation has finished starts again where the
 * level starts, with the level and the shared record as they are. */
static int keen_respawn(void)
{
    int32_t x, y;
    if (coop_anyone_exited() || !coop_level_start(&x, &y))
        return 0;                       /* somebody is out: this death ends the level */
    int p = (int)(plr - players);
    memset(&cur, 0, sizeof cur);
    cur.type = OT_KEEN;
    cur.active = 1;
    cur.x = x;
    cur.y = y;
    cur.think = keen_walk;
    cur.touch = keen_touch;
    cur.sprite = 4;
    plr->facing = 1;
    plr->invuln = 0;
    objs[p] = cur;                      /* camera_start looks at the object */
    camera_start(p);
    return 1;
}

/* 0x4281: dying animation */
void keen_dying(void)
{
    cur.timer += tics;
    if (cur.timer > 199) {
        cur.timer = -999;
        cur.xv = rand8() - 0x80;
        cur.yv = -400;
    }
    cur.sprite = 0x16 + ((gtick >> 4) & 1);
    cur.dx = cur.xv * tics;
    cur.dy = cur.yv * tics;
    if (cur.y2 < plr->cam_y) {          /* off the top of his own view */
        if (game.coop && keen_respawn())
            return;
        cur.type = 0;
        plr->status = PS_DEAD;
    }
}

/* 0x468D: Keen touching another object */
void keen_touch(obj_t *self, obj_t *o)
{
    switch (o->type) {
    case OT_YORP:
        if (o->think == yorp_stunned)
            return;
        if (self->yv > 0 && self->y + 0x800 <= o->y) {
            o->think = yorp_stunned;
            o->timer = 0;
            self->yv = 0;
            self->think = keen_walk;
            snd_play(0x1f);
            return;
        }
        self->yv = 0;
        self->xv = o->xv < 1 ? -0xf0 : 0xf0;
        snd_play(0x1d);
        return;
    case OT_GARG:
    case OT_VORT:
    case OT_ENEMYSHOT:
        if (o->type == OT_ENEMYSHOT && self->think == keen_frozen) {
            self->timer = 0;
            return;
        }
        kill_keen(self);
        return;
    case OT_BUTLER:
    case OT_TANK:
        self->yv = 0;
        self->xv = o->xv < 1 ? -0xf0 : 0xf0;
        snd_play(0x1d);
        return;
    case OT_ICEBLOCK:
        self->think = keen_frozen;
        self->xv = o->xv;
        self->yv = o->yv;
        self->timer = 800;
        snd_play(0x28);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Shots: 0x4783 Keen's raygun, 0x48B1..0x4912 shared shot logic */

void think_splash(void)
{
    cur.type = OT_DEAD;
    cur.timer += tics;
    if (cur.timer > 0x14)
        cur.type = 0;
}

static void shot_splash_obj(obj_t *o)
{
    snd_play(0x25);
    o->think = think_splash;
    o->touch = touch_none;
    o->timer = 0;
    o->sprite = rand8() > 0x80 ? 0x6e : 0x6f;
}

void think_shot(void)
{
    if (move_obj()) {
        snd_play(0x25);
        cur.type = OT_SPLASH;
        cur.think = think_splash;
        cur.timer = 0;
        cur.sprite = rand8() > 0x80 ? 0x6e : 0x6f;
    }
}

void keenshot_touch(obj_t *self, obj_t *o)
{
    if (o->type != OT_KEEN && o->type != OT_DEAD)
        shot_splash_obj(self);
}

static void spawn_keenshot(int32_t x, int32_t y)
{
    obj_t *o = new_obj();
    o->type = OT_KEENSHOT;
    o->x = x;
    o->y = y + 0x900;
    o->think = think_shot;
    o->yv = 0;
    o->touch = keenshot_touch;
    o->sprite = 0x6c;
    int row = (int)(y >> 12) + 1;
    if (plr->facing >= 0) {
        o->xv = 0x190;
        if (!T_RIGHT(tile_at((int)(x >> 12) + 1, row)))
            return;
    } else {
        o->xv = -0x190;
        if (!T_LEFT(tile_at((int)(x >> 12), row)))
            return;
    }
    snd_play(0x25);
    o->type = OT_SPLASH;
    o->think = think_splash;
    o->timer = 0;
    o->sprite = rand8() > 0x80 ? 0x6e : 0x6f;
}

/* 0x4957 / 0x4966: shot enemies */
void think_dead(void)
{
    cur.timer++;
    gravity();
    move_obj();
}

void think_dying(void)
{
    cur.type = OT_DEAD;
    cur.timer += tics;
    if (cur.timer > 0x28) {
        cur.timer -= 0x28;
        cur.sprite++;
        cur.w2c--;
        if (cur.w2c == 1)
            cur.think = think_dead;
    }
    cur.xv = 0;
    gravity();
    move_obj();
}

/* ------------------------------------------------------------------ */
/* Switches and bridges (0x365B), lights (0x361B/0x363B) */

static const uint8_t *pal_full(void);
static const uint8_t *pal_dark(void);

static void lights(int on)
{
    lights_on = on;
    wait_vbl(1);
    plat_set_palette(on ? pal_full() : pal_dark());
}

static void switch_activate(void)
{
    snd_play(0x19);
    if (tile_at(plr->switch_x + 3, plr->switch_y + 5) == 0x1df && game.coop) {
        /* Co-op: a timer in the game state shakes everybody's screen (see
         * draw_camera) under a banner, then the level ends. */
        if (!game.shake) {
            game.shake = 80;
            banner_add(BN_CASTLE);
        }
        return;
    }
    if (tile_at(plr->switch_x + 3, plr->switch_y + 5) == 0x1df) {
        /* The switch in the castle shakes the screen and ends the level. */
        int32_t sx = scroll_x, sy = scroll_y;
        for (int i = 1; i < 0x50; i++) {
            frame_start();
            scroll_x = sx - ((rand_range(0x40) - 0x20) << 8);
            scroll_y = sy - ((rand_range(0x40) - 0x20) << 8);
            rf_sprite(cur.x, cur.y, cur.sprite);
            frame_refresh();
        }
        win_grow(5, 1);
        print(ds_str(0x2408));
        wait_vbl(100);
        level_result = 3;
        return;
    }
    int t = tile_at(plr->switch_x, plr->switch_y);
    if (t == 0x10f) {
        lights(!lights_on);
        return;
    }
    if (t == 0x1e0)
        set_tile(plr->switch_x, plr->switch_y, 0x1ed);
    else if (t == 0x1ed)
        set_tile(plr->switch_x, plr->switch_y, 0x1e0);
    int info = info_at(plr->switch_x, plr->switch_y);
    int bx = plr->switch_x + (int8_t)(info & 0xff);
    int by = plr->switch_y + (int8_t)(info >> 8);
    int i;
    for (i = 0; i < numspecs; i++)
        if (specs[i].active && specs[i].x == bx && specs[i].y == by)
            break;
    if (i < numspecs) {
        specs[i].think = specs[i].think == spec_bridge_retract ? spec_bridge_extend
                                                                 : spec_bridge_retract;
        return;
    }
    spec_t *s = new_spec();
    s->active = 2;
    s->think = spec_bridge_extend;
    s->x = bx;
    s->y = by;
    s->cnt = 0;
    s->w6 = T_LEFT(tile_at(bx + 1, by)) ? -1 : 1;
    s->w7 = tile_at(bx, by);
    s->w8 = 0;
}

/* 0x2863: open a keycard door at tile (tx, ty). */
static void open_door(int tx, int ty)
{
    snd_play_always(0x21);              /* heard by everyone: the way is open */
    int b = T_BEHAV(tile_at(tx, ty));
    int above = T_BEHAV(tile_at(tx, ty - 1));
    int top = above == b ? ty - 1 : ty;
    spec_t *s = new_spec();
    s->active = 1;
    s->think = spec_door;
    s->x = (int32_t)tx << 12;
    s->y = (int32_t)top << 12;
    s->cnt = 0;
    s->w6 = tile_at(tx, top + 3);
    s->w7 = tile_at(tx, top + 2);
    s->w8 = tile_at(tx, top);
    gs.keys[b - 2] = 0;
    set_tile(tx, top, 0x8f);
    set_tile(tx, top + 1, 0x8f);
}

/* 0x4373: Keen i touching special tiles. Everything a Keen collects goes into
 * the one shared game record `gs`, whoever touched it. */
static void keen_tiles(int i)
{
    obj_t *k = &objs[i];
    cur_plr = &players[i];
    if (k->type != OT_KEEN || k->think == keen_dying)
        return;
    plr->on_switch = 0;
    int tx1 = k->x1 >> 12, tx2 = k->x2 >> 12, ty1 = k->y1 >> 12, ty2 = k->y2 >> 12;
    for (int tx = tx1; tx <= tx2; tx++)
        for (int ty = ty1; ty <= ty2; ty++) {
            int t = tile_at(tx, ty);
            int b = T_BEHAV(t);
            switch (b) {
            case 0:
            case -1:
                break;
            case 1:
                kill_keen(k);
                break;
            case 2: case 3: case 4: case 5:
                if (!gs.keys[b - 2]) {
                    if (k->dx < 1)
                        k->x = (k->x + 0x1000) & ~0xfff;
                    else
                        k->x &= ~0xfff;
                } else {
                    open_door(tx, ty);
                }
                break;
            case 6: case 7: case 8: case 9: case 10:
                add_score(ds_w(0x1fde + b * 2));
                snd_play(9);
                set_tile(tx, ty, t < 0x131 ? 0x8f : 0x114);
                break;
            case 11: case 12: case 13: case 14:
                if (b == 11) gs.joystick = 1;
                if (b == 12) gs.battery = 1;
                if (b == 13) gs.vacuum = 1;
                if (b == 14) gs.whiskey = 1;
                add_score(10000);
                snd_play(0xb);
                set_tile(tx, ty, 0x8f);
                break;
            case 15: case 16:
                if (b == 15) gs.ammo += game.coop ? 7 : 5;  /* co-op: more shots go astray */
                if (b == 16) gs.pogo = 1;
                snd_play(10);
                set_tile(tx, ty, 0x8f);
                break;
            case 17:
                if (k->think == keen_walk) {
                    snd_play_always(0xf);   /* heard by everyone, like a death */
                    k->think = keen_exit_walk;
                    k->touch = touch_none;
                    k->timer = tx + 2;
                    k->w2c = ty;
                }
                break;
            case 18: case 19: case 20: case 21:
                gs.keys[b - 18] = 1;
                snd_play(0x20);
                set_tile(tx, ty, 0x8f);
                break;
            case 22:
                if (game.coop)
                    banner_add(BN_STATUE);
                else
                    statue_message();
                set_tile(tx, ty, cur_level == 11 ? 0x1b2 : 0x13b);
                break;
            case 23: case 25: case 26:
                plr->on_switch = 1;
                plr->switch_x = tx;
                plr->switch_y = ty;
                break;
            case 24:
                level_result = 2;
                break;
            case 27:
                plr->invuln += 0x578;
                set_tile(tx, ty, t / 13 * 13);
                break;
            case 28:
                gs.ammo++;
                snd_play(10);
                break;
            }
        }
}

/* ------------------------------------------------------------------ */
/* Palettes from the EXE: 24CC black, 24DD dark, 24EE half, 24FF full. */

static uint8_t pals[4][17];
static const uint8_t *pal_full(void) { return pals[3]; }
static const uint8_t *pal_dark(void) { return pals[1]; }
const uint8_t *game_palette(int n) { return pals[n]; }

void palettes_init(void)
{
    for (int p = 0; p < 4; p++)
        for (int i = 0; i < 17; i++)
            pals[p][i] = ds_b(0x24cc + p * 17 + i);
}

/* ------------------------------------------------------------------ */
/* 0x4C32: play one level. Returns 0 died, 1 done, 2 teleported, 3 left. */

/* Draw Keen `slot`; with more than one player each Keen gets his colour
 * scheme and a P1/P2 tag above him (levels and the world map). */
void draw_keen(int32_t x, int32_t y, int sprite, int slot)
{
    if (nplayers > 1) {
        char tag[3] = {'P', (char)('1' + slot), 0};
        rf_sprite_ex(x, y, sprite, slot & 1);
        rf_label(x, y - 0x800, tag);
        return;
    }
    rf_sprite(x, y, sprite);
}

static void draw_obj(const obj_t *o, int slot)
{
    if (slot < nplayers && o->type == OT_KEEN)
        draw_keen(o->x, o->y, o->sprite, slot);
    else
        rf_sprite(o->x, o->y, o->sprite);
}

void camera_start(int i)
{
    player_t *p = &players[i];
    p->cam_x = objs[i].x - 0xa000;
    p->cam_y = objs[i].y - 0x5000;
    if (p->cam_x < scrollmin_x) p->cam_x = scrollmin_x;
    if (p->cam_y < scrollmin_y) p->cam_y = scrollmin_y;
    if (p->cam_x > scrollmax_x) p->cam_x = scrollmax_x;
    if (p->cam_y > scrollmax_y) p->cam_y = scrollmax_y;
    p->cam_tx = p->cam_x >> 12;
    p->cam_ty = p->cam_y >> 12;
}

/* The camera that gets drawn is the local player's. While the castle switch
 * shakes the screen the picture jitters; the jitter comes from a generator of
 * its own so it never touches the game's random numbers. */
static uint32_t jitter_state = 1;

static int jitter(void)
{
    static int off = -1;                /* KEEN_NOJITTER: test aid, draws without shaking */
    if (off < 0)
        off = getenv("KEEN_NOJITTER") != NULL;
    if (off)
        return 0;
    jitter_state = jitter_state * 1664525u + 1013904223u;
    return (int)(jitter_state >> 24) % 0x40 - 0x20;
}

void draw_camera(void)
{
    scroll_x = players[local_player].cam_x;
    scroll_y = players[local_player].cam_y;
    if (game.shake) {
        scroll_x -= jitter() << 8;
        scroll_y -= jitter() << 8;
    }
}

/* In a level each player hears only what happens on his own screen (with a
 * tile to spare): every sound has a source, set before its object thinks or
 * touches. His own Keen is always heard; deaths, doors and the exit door
 * are heard by everybody (snd_play_always). Sound is not game state. */
static int heard_at(int32_t x, int32_t y)
{
    const player_t *p = &players[local_player];
    return x > p->cam_x - 0x1000 && x < p->cam_x + 0x15000 &&
           y > p->cam_y - 0x1000 && y < p->cam_y + 0xd800;
}

static void snd_source(const obj_t *o)
{
    snd_muted = game.coop && o != &objs[local_player] && !heard_at(o->x, o->y);
}

static int play_level_run(int num)
{
    /* The Keens' old positions survive (actors spawned before the start
     * marker face them based on it, as in the original). */
    int32_t kx[MAXPLAYERS], ky[MAXPLAYERS];
    for (int i = 0; i < nplayers; i++) {
        kx[i] = objs[i].x;
        ky[i] = objs[i].y;
    }
    rf_hook = game.coop ? ui_overlay_hook : NULL;
    memset(objs, 0, sizeof objs);
    memset(specs, 0, sizeof specs);
    if (game.coop)
        coop_level_begin();
    for (int i = 0; i < nplayers; i++) {
        obj_t *k = &objs[i];
        players[i].facing = 1;
        players[i].invuln = 0;
        players[i].ctrl = players[i].ctrl_prev = (ctrl_t){8, 0, 0, 0};
        if (!PRESENT(i))
            continue;               /* nobody in this seat: no Keen */
        k->x = kx[i];
        k->y = ky[i];
        k->type = OT_KEEN;
        k->think = keen_walk;
        k->touch = keen_touch;
        k->sprite = 4;
        k->active = 1;
    }
    numobjs = nplayers;
    numspecs = 0;
    tics = 0;
    godmode = 0;
    level_result = 0;
    gtick = 0;
    frame_no = 0;
    plat_set_ticks(0);      /* pacing only */
    lasttick = 0;
    load_level(num);
    spawn_actors();

    for (int i = 0; i < nplayers; i++)
        if (PRESENT(i))
            camera_start(i);
    draw_camera();

    /* Test aid for writing replays: KEEN_FIND=<behaviour> lists where those tiles are. */
    if (replaying && getenv("KEEN_FIND")) {
        const char *arg = getenv("KEEN_FIND");
        if (arg[0] == 'l') {                /* "l23": the tile numbers that have behaviour 23 */
            printf("tile numbers with behaviour %d:", atoi(arg + 1));
            for (int t = 0; t < 700; t++)
                if (T_BEHAV(t) == atoi(arg + 1))
                    printf(" 0x%x", t);
            printf("\n");
        }
        int tile = arg[0] == 't';           /* "t0x1df": that tile number instead */
        int want = (int)strtol(tile ? arg + 1 : arg, NULL, 0);
        printf("level %d: tiles with %s %d at:", num, tile ? "number" : "behaviour", want);
        for (int ty = 0; ty < map_h; ty++)
            for (int tx = 0; tx < map_w; tx++)
                if ((tile ? MAPT(tx, ty) : T_BEHAV(MAPT(tx, ty))) == want)
                    printf(" %d,%d", tx, ty);
        printf("\n");
    }

    frame_start();
    if (!warp_level)
        fade_out();
    lights_on = 1;
    frame_refresh();
    frame_refresh();
    fade_in();

    if (net_online) {
        net_segment_begin();
        net_restore_apply();        /* a joiner or a repaired browser continues from a snapshot */
    }

    for (;;) {
        if (net_online && net_frame_begin())
            return LR_RESTORE;      /* a snapshot arrived: play_game starts this level from it */
        frame_start();
        if (replaying)
            replay_apply();
        else
            sim_apply_script_warps();
        for (int i = 0; i < nplayers; i++)
            read_player_ctrl(i);
        record_frame();
        if (game.coop)
            coop_events(1);
        for (int i = 0; i < numobjs; i++) {
            obj_t *o = &objs[i];
            if (!o->type)
                continue;
            cur = *o;
            if (i < nplayers)
                cur_plr = &players[i];
            snd_source(o);
            if (!cur.active) {
                int tx = cur.x >> 12, ty = cur.y >> 12;
                /* Wakes when it enters any player's camera view. */
                if (cam_sees(tx, ty, 2, 2, 0x17, 0xc)) {
                    cur.active = 1;
                    if (cur.think == yorp_stunned) {
                        cur.think = yorp_look;
                        cur.timer = 0;
                    }
                    calc_hitbox();
                    cur.dx = cur.dy = 0;
                    cur.think();
                    cur.x += cur.dx;
                    cur.y += cur.dy;
                    calc_hitbox();
                }
            } else if (i < nplayers || !obj_offscreen()) {
                calc_hitbox();
                cur.dx = cur.dy = 0;
                cur.think();
                cur.x += cur.dx;
                cur.y += cur.dy;
                calc_hitbox();
            }
            *o = cur;
        }
        for (int i = 0; i < nplayers; i++)
            if (objs[i].type)
                scroll_follow(i);
        if (game.coop)
            coop_follow_cameras();
        for (int i = 0; i < numobjs; i++) {
            obj_t *a = &objs[i];
            if (!a->type || !a->active)
                continue;
            for (int j = i + 1; j < numobjs; j++) {
                obj_t *b = &objs[j];
                if (b->type && b->active && touching(a, b)) {
                    snd_source(a);
                    a->touch(a, b);
                    snd_source(b);
                    b->touch(b, a);
                }
            }
        }
        for (int i = 0; i < nplayers; i++) {
            players[i].cam_tx = players[i].cam_x >> 12;
            players[i].cam_ty = players[i].cam_y >> 12;
        }
        draw_camera();
        for (int i = numobjs - 1; i >= 0; i--)
            if (objs[i].type && objs[i].active)
                draw_obj(&objs[i], i);
        for (int i = 0; i < nplayers; i++)
            if (objs[i].type) {
                snd_source(&objs[i]);
                keen_tiles(i);
            }
        for (int i = 0; i < numspecs; i++)
            if (specs[i].active) {
                /* Only the ice cannons make sounds; they keep tile coordinates. */
                snd_muted = game.coop && !heard_at((int32_t)specs[i].x << 12, (int32_t)specs[i].y << 12);
                specs[i].think(&specs[i]);
            }
        snd_muted = 0;
        if (game.coop)
            coop_tick();
        frame_refresh();
        for (int i = 0; i < nplayers; i++)
            players[i].ctrl_prev = players[i].ctrl;
        frame_no++;
        replay_frame_end();
        if (!replaying) {
            if (game.coop) {
                ui_poll_coop();
            } else {
                check_cheats();
                if (check_hotkeys())
                    reset_frame_time();
            }
        }
        if (abort_game)
            return 0;
        int res;
        if (game.coop) {
            res = coop_level_eval();
        } else {
            int alive = 0;
            for (int i = 0; i < nplayers; i++)
                alive |= objs[i].type != 0;
            res = level_result || !alive ? level_result : -1;
        }
        if (res < 0 && replaying && replay_level_done())
            return LR_STOPPED;
        if (res >= 0) {
            snd_wait();
            fade_out();
            if (game.coop && (res == LR_FAIL || res == LR_VOTE_LEAVE))
                coop_rollback();
            for (int k = 0; k < 4; k++)
                gs.keys[k] = 0;
            if (res == LR_DIED) {
                if (num == 8)
                    gs.vacuum = 0;
                else if (num == 3)
                    gs.battery = 0;
                else if (num == 4)
                    gs.joystick = 0;
                else if (num == 16)
                    gs.whiskey = 0;
            }
            rf_hook = NULL;
            return res;
        }
    }
}

/* Every level starts its random generator from a seed: the game seed and the
 * count of levels started, or the seed a replay prescribes. */
uint32_t game_seed;
extern uint32_t level_seed_override;
extern int level_seed_set;

int play_level(int num)
{
    if (replaying && replay_is_flow() && !replay_next_segment(0, num))
        return LR_STOPPED;
    game.levels_started++;          /* part of the game state: a joiner must know it */
    uint32_t seed = level_seed_set ? level_seed_override
                                   : game_seed + 0x9e3779b1u * (uint32_t)game.levels_started;
    rng_seed(seed);
    record_level_begin(num, seed);
    int r = play_level_run(num);
    record_level_end();
    return r;
}
