/* Deterministic simulation support: function registry, state snapshots and
 * checksums, recorded-input replay (headless) and recording.
 *
 * A snapshot holds everything that decides what the next frame looks like:
 * objects, special objects, both level planes, the shared game record `gs`,
 * the players (with their cameras), the random generator, the frame counters
 * and the level-scope globals. Function pointers are written as ids. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "kdata.h"
#include "platform.h"

int replaying;
int headless_delay_ms;

/* ------------------------------------------------------------------ */
/* Registry of think / touch / special functions */

extern void keen_jump_charge(void), keen_air(void), keen_shoot(void), keen_pogo_air(void),
    keen_pogo_charge(void), keen_frozen(void), keen_exit_walk(void), keen_dying(void),
    think_shot(void);
extern void yorp_walk(void), garg_walk(void), garg_look(void), vort_walk(void), vort_jump(void),
    vort_look(void), butler_walk(void), butler_turn(void), tank_fall(void), tank_walk(void),
    tank_turn(void), tank_fire(void), icechunk_think(void), iceblock_think(void),
    remove_self(void);
extern void keenshot_touch(obj_t *, obj_t *), yorp_touch(obj_t *, obj_t *),
    garg_touch(obj_t *, obj_t *), vort_touch(obj_t *, obj_t *), butler_touch(obj_t *, obj_t *),
    tankshot_touch(obj_t *, obj_t *), iceblock_touch(obj_t *, obj_t *),
    chain_touch(obj_t *, obj_t *);
extern void spec_flash(spec_t *), spec_cannon(spec_t *), spec_dropblock(spec_t *);

static think_t think_tab[] = {
    NULL, think_none, keen_walk, keen_jump_charge, keen_air, keen_shoot, keen_pogo_air,
    keen_pogo_charge, keen_frozen, keen_exit_walk, keen_dying, think_splash, think_shot,
    think_dead, think_dying, yorp_walk, yorp_look, yorp_stunned, garg_walk, garg_look, vort_walk,
    vort_jump, vort_look, butler_walk, butler_turn, tank_fall, tank_walk, tank_turn, tank_fire,
    icechunk_think, iceblock_think, remove_self,
};
static touch_t touch_tab[] = {
    NULL, touch_none, keen_touch, keenshot_touch, yorp_touch, garg_touch, vort_touch,
    butler_touch, tankshot_touch, iceblock_touch, chain_touch,
};
static void (*spec_tab[])(spec_t *) = {
    NULL, spec_none, spec_door, spec_bridge_extend, spec_bridge_retract, spec_flash, spec_cannon,
    spec_dropblock,
};
#define NTAB(t) ((int)(sizeof(t) / sizeof((t)[0])))

int think_id(think_t f)
{
    for (int i = 0; i < NTAB(think_tab); i++)
        if (think_tab[i] == f)
            return i;
    fprintf(stderr, "sim: think function missing from the registry\n");
    abort();
}

think_t think_from_id(int id)
{
    return id >= 0 && id < NTAB(think_tab) ? think_tab[id] : NULL;
}

int touch_id(touch_t f)
{
    for (int i = 0; i < NTAB(touch_tab); i++)
        if (touch_tab[i] == f)
            return i;
    fprintf(stderr, "sim: touch function missing from the registry\n");
    abort();
}

touch_t touch_from_id(int id)
{
    return id >= 0 && id < NTAB(touch_tab) ? touch_tab[id] : NULL;
}

int spec_think_id(void (*f)(spec_t *))
{
    for (int i = 0; i < NTAB(spec_tab); i++)
        if (spec_tab[i] == f)
            return i;
    fprintf(stderr, "sim: special function missing from the registry\n");
    abort();
}

void (*spec_think_from_id(int id))(spec_t *)
{
    return id >= 0 && id < NTAB(spec_tab) ? spec_tab[id] : NULL;
}

/* ------------------------------------------------------------------ */
/* Snapshot: explicit little-endian fields, so the bytes (and the checksum)
 * do not depend on struct padding or the machine. */

extern int rnd_index, rng_a, rng_b;
extern uint16_t rng_state[18], rng_sum;

static uint8_t *sbuf;
static size_t slen, scap;

static void put(const void *p, size_t n)
{
    if (slen + n > scap) {
        scap = (slen + n) * 2 + 4096;
        sbuf = realloc(sbuf, scap);
    }
    memcpy(sbuf + slen, p, n);
    slen += n;
}

static void p32(uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, 4);
}

static void p16(uint32_t v)
{
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(b, 2);
}

#define SNAP_MAGIC 0x4b534e31u /* "KSN1" */

static void save_ctrl(const ctrl_t *c)
{
    p16((uint16_t)c->dir);
    p16((uint16_t)c->b1);
    p16((uint16_t)c->b2);
}

static void serialise(void)
{
    slen = 0;
    p32(SNAP_MAGIC);
    /* level-scope globals */
    p32((uint32_t)map_w);
    p32((uint32_t)map_h);
    p32((uint32_t)cur_level);
    p32((uint32_t)nplayers);
    p32((uint32_t)numobjs);
    p32((uint32_t)numspecs);
    p32(gtick);
    p32(frame_no);
    p32((uint32_t)tics);
    p32((uint32_t)level_result);
    p32((uint32_t)godmode);
    p32((uint32_t)lights_on);
    p32((uint32_t)last_extra);
    p32((uint32_t)scrollmin_x);
    p32((uint32_t)scrollmin_y);
    p32((uint32_t)scrollmax_x);
    p32((uint32_t)scrollmax_y);
    p32((uint32_t)objmax_x);
    p32((uint32_t)objmax_y);
    p32((uint32_t)keenmax_x);
    p32((uint32_t)keendeath_y);
    /* shared game record */
    put(&gs, sizeof gs);
    /* random generator */
    p32((uint32_t)rnd_index);
    p32((uint32_t)rng_a);
    p32((uint32_t)rng_b);
    p16(rng_sum);
    for (int i = 0; i < 18; i++)
        p16(rng_state[i]);
    /* players */
    for (int i = 0; i < nplayers; i++) {
        const player_t *p = &players[i];
        p32((uint32_t)p->facing);
        p32((uint32_t)p->invuln);
        save_ctrl(&p->ctrl);
        save_ctrl(&p->ctrl_prev);
        p32((uint32_t)p->cam_x);
        p32((uint32_t)p->cam_y);
        p32((uint32_t)p->cam_tx);
        p32((uint32_t)p->cam_ty);
        p32((uint32_t)p->map_x);
        p32((uint32_t)p->map_y);
        p32((uint32_t)p->on_switch);
        p32((uint32_t)p->switch_x);
        p32((uint32_t)p->switch_y);
    }
    /* objects */
    for (int i = 0; i < numobjs; i++) {
        const obj_t *o = &objs[i];
        p16((uint16_t)o->type);
        p16((uint16_t)o->active);
        p32((uint32_t)o->x);
        p32((uint32_t)o->y);
        p32((uint32_t)o->x1);
        p32((uint32_t)o->y1);
        p32((uint32_t)o->x2);
        p32((uint32_t)o->y2);
        p16((uint16_t)o->dx);
        p16((uint16_t)o->dy);
        p16((uint16_t)o->xv);
        p16((uint16_t)o->yv);
        p16((uint16_t)o->w24);
        p16((uint16_t)o->w26);
        p16((uint16_t)o->sprite);
        p16((uint16_t)o->timer);
        p16((uint16_t)o->w2c);
        p16((uint16_t)o->w2e);
        p16((uint16_t)o->w30);
        p16((uint16_t)think_id(o->think));
        p16((uint16_t)touch_id(o->touch));
    }
    /* special objects */
    for (int i = 0; i < numspecs; i++) {
        const spec_t *s = &specs[i];
        p32((uint32_t)s->x);
        p32((uint32_t)s->y);
        p16((uint16_t)s->active);
        p16((uint16_t)s->cnt);
        p16((uint16_t)s->w6);
        p16((uint16_t)s->w7);
        p16((uint16_t)s->w8);
        p16((uint16_t)spec_think_id(s->think));
    }
    /* level planes */
    if (map_tiles && map_info) {
        put(map_tiles, (size_t)map_w * map_h * 2);
        put(map_info, (size_t)map_w * map_h * 2);
    }
}

size_t sim_save(uint8_t **buf)
{
    serialise();
    uint8_t *copy = malloc(slen);
    memcpy(copy, sbuf, slen);
    *buf = copy;
    return slen;
}

/* Little-endian reader over a snapshot. */
typedef struct {
    const uint8_t *p, *end;
    int bad;
} rd_t;

static uint32_t r32(rd_t *r)
{
    if (r->p + 4 > r->end) {
        r->bad = 1;
        return 0;
    }
    uint32_t v = r->p[0] | r->p[1] << 8 | (uint32_t)r->p[2] << 16 | (uint32_t)r->p[3] << 24;
    r->p += 4;
    return v;
}

static uint32_t r16(rd_t *r)
{
    if (r->p + 2 > r->end) {
        r->bad = 1;
        return 0;
    }
    uint32_t v = r->p[0] | r->p[1] << 8;
    r->p += 2;
    return v;
}

static void rcopy(rd_t *r, void *dst, size_t n)
{
    if (r->p + n > r->end) {
        r->bad = 1;
        return;
    }
    memcpy(dst, r->p, n);
    r->p += n;
}

static void load_ctrl(rd_t *r, ctrl_t *c)
{
    c->dir = (int16_t)r16(r);
    c->b1 = (int16_t)r16(r);
    c->b2 = (int16_t)r16(r);
}

/* Restores a snapshot taken from the same level. Returns 1 on success. */
int sim_restore(const uint8_t *buf, size_t len)
{
    rd_t r = {buf, buf + len, 0};
    if (r32(&r) != SNAP_MAGIC)
        return 0;
    int mw = (int)r32(&r), mh = (int)r32(&r);
    int lvl = (int)r32(&r);
    int np = (int)r32(&r), no = (int)r32(&r), ns = (int)r32(&r);
    if (np != nplayers || no < 0 || no > MAXOBJS || ns < 0 || ns > MAXSPECS || mw < 1 || mh < 1)
        return 0;
    map_w = mw;
    map_h = mh;
    cur_level = lvl;
    numobjs = no;
    numspecs = ns;
    gtick = r32(&r);
    frame_no = r32(&r);
    tics = (int)r32(&r);
    level_result = (int)r32(&r);
    godmode = (int)r32(&r);
    lights_on = (int)r32(&r);
    last_extra = (int32_t)r32(&r);
    scrollmin_x = (int32_t)r32(&r);
    scrollmin_y = (int32_t)r32(&r);
    scrollmax_x = (int32_t)r32(&r);
    scrollmax_y = (int32_t)r32(&r);
    objmax_x = (int32_t)r32(&r);
    objmax_y = (int32_t)r32(&r);
    keenmax_x = (int32_t)r32(&r);
    keendeath_y = (int32_t)r32(&r);
    rcopy(&r, &gs, sizeof gs);
    rnd_index = (int)r32(&r);
    rng_a = (int)r32(&r);
    rng_b = (int)r32(&r);
    rng_sum = (uint16_t)r16(&r);
    for (int i = 0; i < 18; i++)
        rng_state[i] = (uint16_t)r16(&r);
    for (int i = 0; i < nplayers; i++) {
        player_t *p = &players[i];
        p->facing = (int32_t)r32(&r);
        p->invuln = (int32_t)r32(&r);
        load_ctrl(&r, &p->ctrl);
        load_ctrl(&r, &p->ctrl_prev);
        p->cam_x = (int32_t)r32(&r);
        p->cam_y = (int32_t)r32(&r);
        p->cam_tx = (int32_t)r32(&r);
        p->cam_ty = (int32_t)r32(&r);
        p->map_x = (int32_t)r32(&r);
        p->map_y = (int32_t)r32(&r);
        p->on_switch = (int32_t)r32(&r);
        p->switch_x = (int32_t)r32(&r);
        p->switch_y = (int32_t)r32(&r);
    }
    for (int i = 0; i < numobjs; i++) {
        obj_t *o = &objs[i];
        o->type = (int16_t)r16(&r);
        o->active = (int16_t)r16(&r);
        o->x = (int32_t)r32(&r);
        o->y = (int32_t)r32(&r);
        o->x1 = (int32_t)r32(&r);
        o->y1 = (int32_t)r32(&r);
        o->x2 = (int32_t)r32(&r);
        o->y2 = (int32_t)r32(&r);
        o->dx = (int16_t)r16(&r);
        o->dy = (int16_t)r16(&r);
        o->xv = (int16_t)r16(&r);
        o->yv = (int16_t)r16(&r);
        o->w24 = (int16_t)r16(&r);
        o->w26 = (int16_t)r16(&r);
        o->sprite = (int16_t)r16(&r);
        o->timer = (int16_t)r16(&r);
        o->w2c = (int16_t)r16(&r);
        o->w2e = (int16_t)r16(&r);
        o->w30 = (int16_t)r16(&r);
        o->think = think_from_id((int)r16(&r));
        o->touch = touch_from_id((int)r16(&r));
    }
    for (int i = 0; i < numspecs; i++) {
        spec_t *s = &specs[i];
        s->x = (int32_t)r32(&r);
        s->y = (int32_t)r32(&r);
        s->active = (int16_t)r16(&r);
        s->cnt = (int16_t)r16(&r);
        s->w6 = (int16_t)r16(&r);
        s->w7 = (int16_t)r16(&r);
        s->w8 = (int16_t)r16(&r);
        s->think = spec_think_from_id((int)r16(&r));
    }
    size_t plane = (size_t)map_w * map_h * 2;
    if (!map_tiles || !map_info) {
        map_tiles = malloc(plane);
        map_info = malloc(plane);
    }
    rcopy(&r, map_tiles, plane);
    rcopy(&r, map_info, plane);
    return !r.bad;
}

/* FNV-1a over the serialised snapshot. */
uint32_t sim_checksum(void)
{
    serialise();
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < slen; i++) {
        h ^= sbuf[i];
        h *= 16777619u;
    }
    return h;
}

/* ------------------------------------------------------------------ */
/* Replay files:
 *   # comment
 *   seed N          random seed for the levels that follow
 *   players N       1..MAXPLAYERS
 *   level N         start a segment: play level N
 *   F P DIR B1 B2   from frame F of the segment, player P (1-based) holds
 *                   direction DIR (0..7 clockwise from up, 8 none), buttons B1 B2
 *   warp F P X Y    test aid: at frame F put player P's Keen at tile X,Y
 *   ammo N          test aid: the segment starts with N raygun charges
 *   end F           the segment stops after F frames
 */

typedef struct {
    uint32_t frame;
    int player;
    ctrl_t c;
} rev_t;

typedef struct {
    uint32_t frame;
    int player, tx, ty;
} warp_t;

typedef struct {
    int level;
    uint32_t seed, end;
    rev_t *ev;
    int nev, cap;
    warp_t warp[32];
    int nwarp;
    int ammo;
} seg_t;

#define MAXSEGS 16
static struct {
    int players;
    seg_t seg[MAXSEGS];
    int nseg;
} rp;
static int rp_cur = -1;
uint32_t level_seed_override;
int level_seed_set;

int replay_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Cannot read replay '%s'\n", path);
        return 0;
    }
    char line[256];
    uint32_t seed = 1;
    rp.players = 1;
    rp.nseg = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned a, b, c, d, e;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "seed %u", &a) == 1) {
            seed = a;
        } else if (sscanf(line, "players %u", &a) == 1) {
            rp.players = a < 1 ? 1 : a > MAXPLAYERS ? MAXPLAYERS : (int)a;
        } else if (sscanf(line, "level %u", &a) == 1) {
            if (rp.nseg >= MAXSEGS)
                break;
            seg_t *s = &rp.seg[rp.nseg++];
            memset(s, 0, sizeof *s);
            s->level = (int)a;
            s->seed = seed;
        } else if (rp.nseg && sscanf(line, "warp %u %u %u %u", &a, &b, &c, &d) == 4) {
            seg_t *s = &rp.seg[rp.nseg - 1];
            if (s->nwarp < 32)
                s->warp[s->nwarp++] = (warp_t){a, (int)b - 1, (int)c, (int)d};
        } else if (rp.nseg && sscanf(line, "ammo %u", &a) == 1) {
            rp.seg[rp.nseg - 1].ammo = (int)a;
        } else if (sscanf(line, "end %u", &a) == 1) {
            if (rp.nseg)
                rp.seg[rp.nseg - 1].end = a;
        } else if (rp.nseg && sscanf(line, "%u %u %u %u %u", &a, &b, &c, &d, &e) == 5) {
            seg_t *s = &rp.seg[rp.nseg - 1];
            if (s->nev == s->cap) {
                s->cap = s->cap ? s->cap * 2 : 256;
                s->ev = realloc(s->ev, s->cap * sizeof *s->ev);
            }
            s->ev[s->nev++] = (rev_t){a, (int)b - 1, {(int16_t)c, (int16_t)d, (int16_t)e}};
        }
    }
    fclose(f);
    if (!rp.nseg) {
        fprintf(stderr, "Replay '%s' has no level\n", path);
        return 0;
    }
    return 1;
}

/* Sets this frame's inputs: for each player the latest event at or before
 * frame_no. Depends only on frame_no, so it also works after a restore. */
void replay_apply(void)
{
    if (rp_cur < 0)
        return;
    const seg_t *s = &rp.seg[rp_cur];
    for (int p = 0; p < nplayers; p++) {
        ctrl_t c = {8, 0, 0};
        for (int i = 0; i < s->nev; i++)
            if (s->ev[i].player == p && s->ev[i].frame <= frame_no)
                c = s->ev[i].c;
        players[p].ctrl = c;
    }
    for (int i = 0; i < s->nwarp; i++)
        if (s->warp[i].frame == frame_no && s->warp[i].player < nplayers) {
            obj_t *k = &objs[s->warp[i].player];
            k->x = (int32_t)s->warp[i].tx << 12;
            k->y = (int32_t)s->warp[i].ty << 12;
            k->xv = k->yv = 0;
            camera_start(s->warp[i].player);
        }
}

int replay_level_done(void)
{
    return rp_cur >= 0 && rp.seg[rp_cur].end && frame_no >= rp.seg[rp_cur].end;
}

/* ------------------------------------------------------------------ */
/* Recording: writes the same format as it replays. */

static FILE *recf;
static ctrl_t rec_last[MAXPLAYERS];

static int rec_in_level;

/* Quitting in the middle of a level still leaves a replay that ends. */
static void record_atexit(void)
{
    if (recf && rec_in_level)
        fprintf(recf, "end %u\n", frame_no);
    if (recf)
        fclose(recf);
    recf = NULL;
}

void record_open(const char *path, int level)
{
    (void)level;
    recf = fopen(path, "w");
    if (recf) {
        fprintf(recf, "# keen1-online recording\nplayers %d\n", nplayers);
        atexit(record_atexit);
    }
}

void record_level_begin(int level, uint32_t seed)
{
    if (!recf)
        return;
    fprintf(recf, "seed %u\nlevel %d\n", seed, level);
    rec_in_level = 1;
    for (int i = 0; i < MAXPLAYERS; i++)
        rec_last[i] = (ctrl_t){-1, -1, -1};
}

void record_frame(void)
{
    if (!recf)
        return;
    for (int i = 0; i < nplayers; i++) {
        const ctrl_t *c = &players[i].ctrl;
        if (c->dir != rec_last[i].dir || c->b1 != rec_last[i].b1 || c->b2 != rec_last[i].b2) {
            fprintf(recf, "%u %d %d %d %d\n", frame_no, i + 1, c->dir, c->b1, c->b2);
            rec_last[i] = *c;
        }
    }
}

void record_level_end(void)
{
    if (recf)
        fprintf(recf, "end %u\n", frame_no);
    rec_in_level = 0;
}

void record_close(void)
{
    record_atexit();
}

/* ------------------------------------------------------------------ */
/* Per-frame hook: checksums and the self-test */

static FILE *ckf;
static int selftest_on, selftest_failed;
static uint32_t *st_ck;         /* checksums of the first pass */
static uint32_t st_cap;
static uint8_t *st_snap;
static size_t st_snap_len;
#define ST_SAVE 300
#define ST_END 600

static void selftest_check(int ok, const char *what)
{
    printf("selftest: %-52s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        selftest_failed = 1;
}

/* One object field and one tile changed must change the checksum. */
static void selftest_sensitivity(void)
{
    uint32_t base = sim_checksum();
    selftest_check(sim_checksum() == base, "checksum is stable for an unchanged state");
    objs[0].x += 1;
    selftest_check(sim_checksum() != base, "one object field changed -> checksum changes");
    objs[0].x -= 1;
    selftest_check(sim_checksum() == base, "field restored -> checksum is back");
    uint16_t t = map_tiles[5];
    map_tiles[5] = (uint16_t)(t ^ 1);
    selftest_check(sim_checksum() != base, "one tile changed -> checksum changes");
    map_tiles[5] = t;
    selftest_check(sim_checksum() == base, "tile restored -> checksum is back");
    rng_state[3] ^= 1;
    selftest_check(sim_checksum() != base, "random generator state changed -> checksum changes");
    rng_state[3] ^= 1;
    gtick += 1;
    selftest_check(sim_checksum() != base, "game tick changed -> checksum changes");
    gtick -= 1;
}

void replay_frame_end(void)
{
    if (!ckf && !selftest_on)
        return;
    uint32_t ck = sim_checksum();
    if (ckf)
        fprintf(ckf, "%08x\n", ck);
    if (!selftest_on)
        return;
    if (frame_no >= st_cap) {
        st_cap = frame_no + 1024;
        st_ck = realloc(st_ck, st_cap * sizeof *st_ck);
    }
    static int pass2;
    if (!pass2) {
        st_ck[frame_no] = ck;
        if (frame_no == 50)
            selftest_sensitivity();
        if (frame_no == ST_SAVE) {
            st_snap_len = sim_save(&st_snap);
            printf("selftest: snapshot at frame %d (%zu bytes)\n", ST_SAVE, st_snap_len);
        } else if (frame_no == ST_END) {
            /* back to frame 300 and run the same frames again */
            selftest_check(sim_restore(st_snap, st_snap_len), "snapshot restored");
            selftest_check(frame_no == ST_SAVE, "frame counter is back at the snapshot");
            selftest_check(sim_checksum() == st_ck[ST_SAVE], "checksum right after restore matches");
            pass2 = 1;
        }
    } else {
        static int bad;
        if (ck != st_ck[frame_no] && !bad) {
            bad = 1;
            printf("selftest: re-run differs from the first pass at frame %u\n", frame_no);
        }
        if (frame_no == ST_END) {
            selftest_check(!bad, "re-run of frames 301..600 matches the first pass");
            pass2 = 0;
            selftest_on = 0;
        }
    }
}

/* One checksum per simulated frame goes to this file (any run, not only replays). */
int checksums_open(const char *path)
{
    ckf = fopen(path, "w");
    if (!ckf)
        fprintf(stderr, "Cannot write '%s'\n", path);
    return ckf != NULL;
}

int replay_run(int selftest)
{
    selftest_on = selftest;
    nplayers = rp.players;
    local_player = 0;
    for (int i = 0; i < MAXPLAYERS; i++)
        players[i].input = IN_REPLAY;
    replaying = 1;
    warp_level = 1;          /* title_loop() then starts a game straight away */
    title_loop();
    game_setup();
    /* the Keens come from their world-map positions, as in play_game() */
    for (int i = 0; i < nplayers; i++) {
        objs[i].x = players[i].map_x;
        objs[i].y = players[i].map_y;
    }
    for (int s = 0; s < rp.nseg; s++) {
        rp_cur = s;
        level_seed_override = rp.seg[s].seed;
        level_seed_set = 1;
        if (rp.seg[s].ammo)
            gs.ammo = rp.seg[s].ammo;
        int r = play_level(rp.seg[s].level);
        level_seed_set = 0;
        printf("replay: level %d ended after %u frames, result %d (%s)\n", rp.seg[s].level,
               frame_no, r, r == 0 ? "died" : r == 1 ? "completed" : r == 2 ? "teleported" : "stopped");
        printf("replay:   keys %d%d%d%d score %d ammo %d", gs.keys[0], gs.keys[1], gs.keys[2],
               gs.keys[3], (int)gs.score, gs.ammo);
        for (int i = 0; i < nplayers; i++)
            printf("  P%d: %s", i + 1,
                   objs[i].type == 0 ? "gone" : objs[i].think == keen_dying ? "dying" : "alive");
        printf("\n");
    }
    rp_cur = -1;
    if (ckf)
        fclose(ckf);
    ckf = NULL;
    return selftest_failed;
}

/* --dump-objs: list the objects when the program exits (a quick look at the
 * final positions of Keen and the enemies). */
static void dump_objs(void)
{
    for (int i = 0; i < nplayers; i++)
        printf("player %d map %7d %7d camera %7d %7d\n", i + 1, players[i].map_x, players[i].map_y,
               players[i].cam_x, players[i].cam_y);
    for (int i = 0; i < numobjs; i++)
        if (objs[i].type)
            printf("obj %2d type %2d x %7d y %7d\n", i, objs[i].type, objs[i].x, objs[i].y);
}

void sim_dump_at_exit(void)
{
    atexit(dump_objs);
}
