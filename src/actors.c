/* Enemies and special level objects (KEEN1.EXE 0x1656..0x2863, 0x4957..0x4BC3). */
#include <stdlib.h>

#include "game.h"
#include "gfx.h"
#include "kdata.h"
#include "platform.h"

extern void think_shot(void);

/* Animation frames pick sprites (and so hitboxes): they run on game time. */
static uint32_t tick(void) { return gtick; }

/* Facing helpers: the original compares 32-bit x coordinates with Keen's;
 * with several Keens it is the nearest living one. */
static int keen_left_or_same(void) { return nearest_keen()->x <= cur.x; }
static int keen_left(void) { return nearest_keen()->x < cur.x; }

static void become_corpse(obj_t *self, int sprite, int snd)
{
    self->timer = 0;
    self->w2c = 2;
    self->sprite = sprite;
    self->think = think_dying;
    self->touch = touch_none;
    self->yv = -0x50;
    snd_play(snd);
}

static int is_shot(const obj_t *o)
{
    return o->type == OT_KEENSHOT || o->type == OT_ENEMYSHOT;
}

/* ------------------------------------------------------------------ */
/* Yorp */

void yorp_walk(void);

void yorp_walk(void)
{
    if (cur.yv == 0) {
        if (rand8() < tics) {
            cur.yv = -rand_range(0x80);
        } else if (rand8() < tics) {
            cur.think = yorp_look;
            cur.timer = 0;
        }
    }
    cur.sprite = (cur.xv < 1 ? 0x36 : 0x34) + ((tick() >> 4) & 1);
    gravity();
    int f = move_obj();
    if (f & 4)
        cur.xv = -0x3c;
    if (f & 1)
        cur.xv = 0x3c;
}

void yorp_look(void)
{
    cur.xv = 0;
    cur.sprite = ((tick() >> 5) & 3) + 0x30;
    cur.timer += tics;
    if (cur.timer > 199) {
        cur.xv = keen_left_or_same() ? -0x3c : 0x3c;
        cur.think = yorp_walk;
    }
    gravity();
    move_obj();
}

void yorp_stunned(void)
{
    cur.sprite = ((tick() >> 4) & 1) + 0x38;
    cur.timer += tics;
    if (cur.timer > 799) {
        cur.timer = 0;
        cur.think = yorp_look;
    }
    cur.xv = 0;
    gravity();
    move_obj();
}

void yorp_touch(obj_t *self, obj_t *o)
{
    if (is_shot(o))
        become_corpse(self, 0x3a, 0x22);
}

/* ------------------------------------------------------------------ */
/* Garg */

void garg_look(void);

void garg_walk(void)
{
    if (cur.yv == 0 && cur.xv > -0xdc && cur.xv < 0xdc && rand8() < tics) {
        cur.think = garg_look;
        cur.timer = 0;
    }
    cur.sprite = (cur.xv < 1 ? 0x42 : 0x40) + ((tick() >> 4) & 1);
    gravity();
    int f = move_obj();
    if (f & 4)
        cur.xv = -0x3c;
    if (f & 1)
        cur.xv = 0x3c;
    if (!(f & 2)) {
        if (abs(cur.xv) == 0xdc && cur.timer == 0) {
            cur.timer = 1;
            cur.yv = -200;
        }
    } else {
        cur.timer = 0;
    }
}

void garg_look(void)
{
    cur.xv = 0;
    cur.sprite = ((tick() >> 5) & 3) + 0x3c;
    cur.timer += tics;
    gravity();
    move_obj();
    if (cur.timer > 0x4f) {
        cur.xv = cur.y + 0x800 == nearest_keen()->y ? 0xdc : 0x3c;
        if (keen_left())
            cur.xv = -cur.xv;
        cur.think = garg_walk;
    }
}

void garg_touch(obj_t *self, obj_t *o)
{
    if (is_shot(o))
        become_corpse(self, 0x44, 0x23);
}

/* ------------------------------------------------------------------ */
/* Vorticon */

void vort_jump(void);
void vort_look(void);

void vort_walk(void)
{
    cur.sprite = (cur.xv < 1 ? 0x46 : 0x4a) + ((tick() >> 4) & 3);
    if (rand8() < tics * 2) {
        cur.yv = -rand_range(300);
        cur.think = vort_jump;
    } else if (rand8() < tics * 2) {
        if (cur.xv == 0x5a)
            cur.xv = 0x78;
        else if (cur.xv == 0x78)
            cur.xv = 0x5a;
        else if (cur.xv == -0x78)
            cur.xv = -0x5a;
        else if (cur.xv == -0x5a)
            cur.xv = -0x78;
    }
    gravity();
    int f = move_obj();
    if (f & 4)
        cur.xv = -0x5a;
    if (f & 1)
        cur.xv = 0x5a;
}

void vort_jump(void)
{
    cur.sprite = cur.xv < 1 ? 0x51 : 0x50;
    gravity();
    int f = move_obj();
    if (f & 2) {
        cur.think = vort_look;
        cur.timer = 0;
    }
    if (f & 4)
        cur.xv = -0x5a;
    if (f & 1)
        cur.xv = 0x5a;
}

void vort_look(void)
{
    cur.xv = 0;
    cur.sprite = ((tick() >> 5) & 3) + 0x4e;
    cur.timer += tics;
    if (cur.timer > 0x4f) {
        cur.xv = keen_left() ? -0x5a : 0x5a;
        cur.think = vort_walk;
    }
    gravity();
    move_obj();
}

/* 0x4B0E: border flash when a Vorticon dies. */
void spec_flash(spec_t *s)
{
    s->cnt += tics;
    if (s->cnt > 0x12c) {
        plat_set_border(3);
        s->active = 0;
    } else {
        plat_set_border(((gtick >> 6) & 1) ? 0xe : 3);
    }
}

static void vort_die(obj_t *v)
{
    snd_play(0x27);
    v->timer = 0;
    v->w2c = 6;
    v->sprite = 0x52;
    v->touch = touch_none;
    v->think = think_dying;
}

void vort_touch(obj_t *self, obj_t *o)
{
    if (is_shot(o) && self->w24-- == 0) {
        vort_die(self);
        spec_t *s = new_spec();
        s->active = 5;
        s->think = spec_flash;
        s->cnt = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Butler robot */

void butler_turn(void);

void butler_walk(void)
{
    cur.sprite = (cur.xv < 1 ? 0x5c : 0x58) + ((tick() >> 5) & 3);
    gravity();
    int f = move_obj();
    if (!(f & 2)) {
        cur.dx *= -2;
        cur.dy = 0;
        cur.w2c = -cur.xv;
        cur.think = butler_turn;
        cur.xv = 0;
        cur.timer = 0;
    }
    if (f & 4) {
        cur.w2c = -0x5a;
        cur.think = butler_turn;
        cur.xv = 0;
        cur.timer = 0;
    }
    if (f & 1) {
        cur.w2c = 0x5a;
        cur.think = butler_turn;
        cur.xv = 0;
        cur.timer = 0;
    }
}

void butler_turn(void)
{
    cur.timer += tics;
    if (cur.timer > 0x32) {
        cur.think = butler_walk;
        cur.xv = cur.w2c;
    }
    cur.sprite = ((tick() >> 5) & 1) + 0x60;
    gravity();
    move_obj();
}

void butler_touch(obj_t *self, obj_t *o)
{
    if (o->type == OT_KEENSHOT)
        self->timer = 0;
}

/* ------------------------------------------------------------------ */
/* Tank robot */

void tank_walk(void);
void tank_turn(void);
void tank_fire(void);

void tank_fall(void)
{
    gravity();
    if (move_obj() & 2)
        cur.think = tank_walk;
}

void tank_walk(void)
{
    cur.sprite = (cur.xv < 1 ? 0x66 : 0x62) + ((tick() >> 3) & 3);
    if (rand8() < tics) {
        cur.think = tank_fire;
        cur.timer = 0;
        cur.w2c = 0;
        cur.w2e = cur.xv;
        cur.xv = 0;
    }
    gravity();
    int f = move_obj();
    cur.yv = 0;
    cur.dy = 0;
    if (!(f & 2)) {
        if (cur.sprite < 0x66) {
            cur.dx = tics * -0xb4;
            cur.dy = 0;
            cur.xv = 0;
            cur.yv = 0;
            cur.timer = 0;
            cur.w2c = -0x5a;
            cur.think = tank_turn;
            return;
        }
        cur.dx = tics * 0xb4;
    } else {
        if (f & 4) {
            cur.w2c = -0x5a;
            cur.think = tank_turn;
            cur.xv = 0;
            cur.timer = 0;
        }
        if (!(f & 1)) {
            cur.dy = 0;
            cur.yv = 0;
            return;
        }
    }
    cur.think = tank_turn;
    cur.w2c = 0x5a;
    cur.timer = 0;
    cur.xv = 0;
}

void tank_turn(void)
{
    cur.timer += tics;
    cur.sprite = ((tick() >> 4) & 1) + 0x6a;
    if (cur.timer > 0x32) {
        cur.think = tank_walk;
        cur.xv = cur.w2c;
    }
}

void tank_fire(void)
{
    cur.timer += tics;
    if (cur.w2c == 0 && cur.timer > 0x50) {
        snd_play(0x26);
        spawn_tankshot(cur.x, cur.y, cur.w2e < 0 ? -0x15e : 0x15e);
        cur.w2c = 1;
    }
    if (cur.timer > 0x78) {
        cur.w2c = keen_left() ? -0x5a : 0x5a;
        cur.timer = 0;
        cur.think = tank_turn;
    }
}

void tankshot_touch(obj_t *self, obj_t *o)
{
    if (o->type != OT_TANK && o->type != OT_DEAD) {
        snd_play(0x25);
        self->think = think_splash;
        self->timer = 0;
        self->touch = touch_none;
        self->sprite = rand8() > 0x80 ? 0x6e : 0x6f;
    }
}

void spawn_tankshot(int32_t x, int32_t y, int xv)
{
    obj_t *o = new_obj();
    o->type = OT_ENEMYSHOT;
    o->x = x;
    o->y = y + 0x500;
    o->think = think_shot;
    o->xv = (int16_t)xv;
    o->touch = tankshot_touch;
    o->sprite = 0x6d;
    int row = (int)(y >> 12) + 1;
    int blocked = xv >= 0 ? T_RIGHT(tile_at((int)(x >> 12) + 1, row))
                          : T_LEFT(tile_at((int)(x >> 12), row));
    if (blocked) {
        snd_play(0x25);
        o->type = OT_SPLASH;
        o->think = think_splash;
        o->timer = 0;
        o->sprite = rand8() > 0x80 ? 0x6e : 0x6f;
    }
}

/* ------------------------------------------------------------------ */
/* Ice cannons, ice blocks and chunks */

void remove_self(void) { cur.type = 0; }

void icechunk_think(void)
{
    cur.timer += tics;
    if (cur.timer > 0x3c) {
        cur.type = 0;
        return;
    }
    gravity();
    cur.dx += cur.xv * tics;
    cur.dy += cur.yv * tics;
}

void iceblock_think(void)
{
    if (!move_obj())
        return;
    cur.type = 0;
    static const int16_t v[4][2] = {{300, 300}, {300, -300}, {-300, 300}, {-300, -300}};
    for (int i = 0; i < 4; i++) {
        obj_t *o = new_obj();
        o->type = OT_ICECHUNK;
        o->think = icechunk_think;
        o->touch = touch_none;
        o->x = cur.x;
        o->y = cur.y;
        o->xv = v[i][0];
        o->yv = v[i][1];
        o->sprite = 0x71;
    }
    snd_play(0x13);
}

void iceblock_touch(obj_t *self, obj_t *o)
{
    if (o->type == OT_KEEN)
        self->think = remove_self;
}

static void spawn_iceblock(int32_t x, int32_t y, int dir)
{
    obj_t *o = new_obj();
    o->type = OT_ICEBLOCK;
    o->x = x;
    o->y = y;
    o->think = iceblock_think;
    switch (dir) {
    case 0: o->x += 0x1000; o->yv = -200; o->xv = 200; break;
    case 1: o->yv = -200; o->xv = 0; break;
    case 2: o->yv = 200; o->xv = 0; break;
    case 3: o->yv = -200; o->xv = -200; break;
    }
    o->touch = iceblock_touch;
    o->sprite = 0x70;
}

void spec_cannon(spec_t *s)
{
    int t = (int)(gtick >> 7);
    if (t == s->w6)
        return;
    s->w6 = (int16_t)t;
    int tx = (int16_t)s->x, ty = (int16_t)s->y;
    if (cam_sees(tx, ty, 4, 4, 0x18, 0xe)) {
        snd_play(0x17);
        spawn_iceblock((int32_t)tx << 12, (int32_t)ty << 12, s->cnt);
    }
}

/* ------------------------------------------------------------------ */
/* The chain over the Vorticon Commander (level 16) */

void spec_dropblock(spec_t *s)
{
    int t = (int)(gtick >> 5);
    if (t == s->w6)
        return;
    s->w6 = (int16_t)t;
    int x0 = (int16_t)s->x, y = (int)s->y;
    set_tile(x0 - 4, y, 0x9b);
    set_tile(x0 - 4, y + 1, 0x1dc);
    set_tile(x0 - 4, y + 2, 0x1e9);
    for (int x = x0 - 3; x <= x0 + 3; x++) {
        set_tile(x, y, 0x9b);
        set_tile(x, y + 1, 0x1dd);
        set_tile(x, y + 2, 0x1ea);
    }
    set_tile(x0 + 4, y, 0x9b);
    set_tile(x0 + 4, y + 1, 0x1de);
    set_tile(x0 + 4, y + 2, 0x1eb);
    s->y++;
    s->cnt++;
    if (s->cnt == 4)
        s->active = 0;
}

void chain_touch(obj_t *self, obj_t *o)
{
    if (o->type != OT_KEENSHOT)
        return;
    self->think = think_splash;
    self->timer = 0;
    self->touch = touch_none;
    self->sprite = rand8() > 0x80 ? 0x6e : 0x6f;
    spec_t *s = new_spec();
    s->active = 4;
    s->think = spec_dropblock;
    s->x = self->x >> 12;
    s->y = (self->y >> 12) + 1;
    s->cnt = 0;
    for (int i = numobjs - 1; i >= 0; i--)
        if (objs[i].type == OT_VORT) {
            vort_die(&objs[i]);
            break;
        }
}

/* ------------------------------------------------------------------ */
/* Doors and bridges */

void spec_door(spec_t *s)
{
    s->cnt += tics;
    int off = s->cnt / 5;
    if (off > 0x20) {
        off = 0x20;
        s->active = 0;
    }
    rf_tile(s->x, s->y + off * 256, s->w8);
    rf_tile(s->x, s->y + off * 256 + 0x1000, s->w8 + 1);
    rf_tile(s->x, s->y + 0x2000, s->w7);
    rf_tile(s->x, s->y + 0x3000, s->w6);
}

void spec_bridge_extend(spec_t *s)
{
    s->cnt += tics;
    if (s->cnt > 0xb) {
        s->cnt -= 0xc;
        int tx = (int)s->x + s->w8, ty = (int)s->y;
        if (tile_at(tx, ty) == s->w7) {
            set_tile(tx, ty, 0x10e);
            s->w8 += s->w6;
        } else {
            s->think = spec_none;
        }
    }
}

void spec_bridge_retract(spec_t *s)
{
    s->cnt += tics;
    if (s->cnt > 0xb) {
        s->cnt -= 0xc;
        s->w8 -= s->w6;
        int tx = (int)s->x + s->w8, ty = (int)s->y;
        if (tile_at(tx, ty) == 0x10e)
            set_tile(tx, ty, s->w7);
        else
            s->active = 0;
    }
}

/* ------------------------------------------------------------------ */
/* 0x1656: create objects from the level's info plane. */

/* Actors spawned before the start marker face the Keens' old positions. */
static obj_t *nearest_keen_to(int32_t x)
{
    obj_t *best = &objs[0];
    for (int i = 0; i < nplayers; i++) {
        if (!objs[i].type)
            continue;
        int32_t d = objs[i].x > x ? objs[i].x - x : x - objs[i].x;
        int32_t bd = best->type ? (best->x > x ? best->x - x : x - best->x) : 0x7fffffff;
        if (d < bd)
            best = &objs[i];
    }
    return best;
}

static obj_t *spawn(int type, int tx, int ty, int yoff, int sprite)
{
    obj_t *o = new_obj();
    o->type = (int16_t)type;
    o->x = (int32_t)tx << 12;
    o->y = ((int32_t)ty << 12) + yoff;
    o->sprite = (int16_t)sprite;
    return o;
}

void spawn_actors(void)
{
    for (int ty = 0; ty < map_h; ty++)
        for (int tx = 0; tx < map_w; tx++) {
            int v = MAPI(tx, ty);
            obj_t *o;
            switch (v) {
            case 1:
                o = spawn(OT_YORP, tx, ty, 0x800, 0x30);
                o->think = yorp_walk;
                o->xv = nearest_keen_to(o->x)->x <= o->x ? -0x3c : 0x3c;
                o->touch = yorp_touch;
                break;
            case 2:
                o = spawn(OT_GARG, tx, ty, 0, 0x3c);
                o->think = garg_look;
                o->touch = garg_touch;
                break;
            case 3:
                o = spawn(OT_VORT, tx, ty, 0, 0x4e);
                o->think = vort_walk;
                o->touch = vort_touch;
                o->w24 = cur_level == 16 ? 0x68 : 3;
                o->xv = o->x <= nearest_keen_to(o->x)->x ? 0x5a : -0x5a;
                break;
            case 4:
                o = spawn(OT_BUTLER, tx, ty, 0, 0x60);
                o->xv = o->x < nearest_keen_to(o->x)->x ? -0x5a : 0x5a;
                o->think = butler_walk;
                o->touch = butler_touch;
                break;
            case 5:
                o = spawn(OT_TANK, tx, ty, 0x800, 0x6a);
                o->xv = o->x < nearest_keen_to(o->x)->x ? -0x5a : 0x5a;
                o->think = tank_fall;
                o->touch = touch_none;
                break;
            case 6: case 7: case 8: case 9: {
                spec_t *s = new_spec();
                s->active = 3;
                s->think = spec_cannon;
                s->cnt = (int16_t)(v - 6);
                s->x = tx;
                s->y = ty;
                break;
            }
            case 10:
                o = spawn(OT_CHAIN, tx, ty, 0, 0x72);
                o->think = think_none;
                o->touch = chain_touch;
                break;
            case 0xff:
                for (int i = 0; i < nplayers; i++) {
                    objs[i].x = (int32_t)tx << 12;
                    objs[i].y = ((int32_t)ty << 12) + 0x800;
                }
                break;
            }
        }
}
