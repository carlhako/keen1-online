/* Online play (openspec change online-lobby-netplay).
 *
 * The game runs in lockstep. A server relays every player's input for each frame and "seals" a
 * frame once it has them all; this machine simulates sealed frames only, in order, so everybody
 * runs the same game. Our own input goes out `lead` frames before the frame it is for (2 frames
 * normally, more on a slow connection), so it is usually back before we need it.
 *
 * Everything the server decides arrives inside the frame stream: the first frame starts the
 * game, and `add` and `remove` events bring a player in or take one out on an exact frame.
 *
 * A browser that joins, or that drifted, gets a snapshot (a checkpoint of the game) plus the
 * sealed frames since. It restores the snapshot and fast-forwards through the frames without
 * drawing or sound until it is level with everybody else.
 *
 * Two backends sit behind the same calls: the browser page (web/net.js, through EM_JS) and, for
 * native tests, a frame log written by the server (`--netlog`). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "kdata.h"
#include "platform.h"

/* The page's bridge (web/net.js) also drives the Node test build, `make nodenet`. */
#if defined(__EMSCRIPTEN__) && (!defined(KEEN_NODE) || defined(KEEN_NODE_NET))
#include <emscripten.h>
#define NET_BROWSER 1
#endif

int net_online;
int net_seat;
uint32_t net_frame;
int net_fast;

/* Bits of an input: direction (0..8), the two buttons, and the one-frame events. */
#define IN_DIR(b) ((b) & 15)
#define IN_B1(b) (((b) >> 4) & 1)
#define IN_B2(b) (((b) >> 5) & 1)
#define IN_EV(b) (((b) >> 6) & 31)
static int pack_ctrl(const ctrl_t *c)
{
    return (c->dir & 15) | (c->b1 ? 16 : 0) | (c->b2 ? 32 : 0) | ((c->ev & 31) << 6);
}

enum { EVT_START = 1, EVT_ADD = 2, EVT_REMOVE = 3 };
#define MAX_NET_EVENTS 8

/* A sealed frame as the backends hand it over: [inputs of seat 0 and 1, number of events,
 * then (kind, argument) pairs]. */
typedef int32_t sealed_t[3 + 2 * MAX_NET_EVENTS];

/* Where the game starts or restarts: from the beginning (fresh), or from a snapshot. */
typedef struct {
    uint32_t frame;
    int fresh, seat, joiner;
    uint32_t seed;
    int mask;
    uint32_t snaplen;
} start_t;

typedef struct {
    int (*boot)(start_t *s);                    /* 1 when the start information is there */
    void (*boot_data)(uint8_t *dst);
    void (*send_input)(uint32_t f, int bits);
    int (*frame)(uint32_t f, sealed_t out);     /* 1 when sealed frame f is there */
    int (*backlog)(uint32_t next);              /* sealed frames waiting from `next` on */
    void (*wait)(uint32_t f);
    void (*sum)(uint32_t f, uint32_t c, int loc, int score);
    void (*upload)(int kind, uint32_t f, uint32_t sum, const uint8_t *data, size_t len);
    int (*snap_wanted)(void);
    int (*restore)(start_t *s);                 /* a restore arrived while we play */
    void (*restore_data)(uint8_t *dst);
    void (*ready)(uint32_t f);
    int (*golive)(void);                        /* the frame our input is needed from, or -1 */
    int (*duty)(void);
    int (*lead)(void);
    int (*checkpoint_frames)(void);
    void (*event)(int code);
    int (*local_input)(uint32_t f);             /* a scripted input for frame f (a test bot), or -1 */
    int (*command)(void);                       /* a test command for the game, or 0 */
    int (*command_arg)(int i);                  /* an argument of that command */
    int (*boot_record)(uint8_t *dst);           /* the record a loaded save starts from: its size, 0 for none */
    int (*game_id)(void);
    void (*save)(const char *name, const uint8_t *rec, size_t len);
    int (*save_result)(void);                   /* -1 waiting, 0 refused, 1 stored */
    int (*ended)(void);                         /* -2 not yet, else the seat typing the team name or -1 */
    void (*submit_score)(const char *name, int32_t score, int parts);
    int (*scores)(net_score_t *rows);           /* 1 when the server's table is there */
    void (*chat_send)(const char *text);
    int (*chat_take)(char *text, int max);      /* a chat line that arrived: its seat, or -1 */
} backend_t;

enum { NE_ENDING = 1, NE_FINISHED = 2, NE_LEAVE = 3 };
enum { UP_CHECKPOINT = 1, UP_RESYNC = 2 };

/* ------------------------------------------------------------------ */
/* The browser */

#ifdef NET_BROWSER

EM_JS(int, js_boot, (int32_t *out), {
    var n = Module.net;
    if (!n.bootReady())
        return 0;
    var b = n.bootInfo();
    var o = out >> 2;
    HEAP32[o] = b.f;
    HEAP32[o + 1] = b.fresh ? 1 : 0;
    HEAP32[o + 2] = n.game ? n.game.seat : 0;
    HEAP32[o + 3] = b.seed | 0;
    HEAP32[o + 4] = b.mask;
    HEAP32[o + 5] = b.snap ? b.snap.length : 0;
    HEAP32[o + 6] = b.joiner ? 1 : 0;
    return 1;
});
EM_JS(void, js_boot_data, (uint8_t *dst), {
    var b = Module.net.bootInfo();
    if (b.snap)
        HEAPU8.set(b.snap, dst);
});
EM_JS(void, js_send_input, (uint32_t f, int bits), { Module.net.sendInput(f >>> 0, bits); });
EM_JS(int, js_frame, (uint32_t f, int32_t *out), {
    var m = Module.net.frame(f >>> 0);
    if (!m)
        return 0;
    var o = out >> 2;
    HEAP32[o] = m['in'][0];
    HEAP32[o + 1] = m['in'][1];
    var ev = m.ev || [], k = 0;
    for (var i = 0; i < ev.length && k < 8; i++) {
        var e = ev[i];
        if (e.t === 'start') { HEAP32[o + 3 + 2 * k] = 1; HEAP32[o + 4 + 2 * k] = e.mask; k++; }
        else if (e.t === 'add') { HEAP32[o + 3 + 2 * k] = 2; HEAP32[o + 4 + 2 * k] = e.seat; k++; }
        else if (e.t === 'remove') { HEAP32[o + 3 + 2 * k] = 3; HEAP32[o + 4 + 2 * k] = e.seat; k++; }
    }
    HEAP32[o + 2] = k;
    return 1;
});
EM_JS(int, js_backlog, (uint32_t next), { return Module.net.backlog(next >>> 0); });
EM_JS(void, js_wait, (uint32_t f), { Module.net.wait(f >>> 0); });
EM_JS(void, js_sum, (uint32_t f, uint32_t c, int loc, int score), {
    Module.net.sum(f >>> 0, c >>> 0, loc, score);
});
EM_JS(void, js_upload, (int kind, uint32_t f, uint32_t sum, const uint8_t *data, int len), {
    Module.net.upload(kind, f >>> 0, sum >>> 0, HEAPU8.slice(data, data + len));
});
EM_JS(int, js_snap_wanted, (void), { return Module.net.takeSnapRequest() ? 1 : 0; });
EM_JS(int, js_restore, (int32_t *out), {
    var r = Module.net.takeRestore();
    if (!r)
        return 0;
    Module.net.pendingBoot = r;
    var o = out >> 2;
    HEAP32[o] = r.f;
    HEAP32[o + 1] = r.fresh ? 1 : 0;
    HEAP32[o + 2] = Module.net.game ? Module.net.game.seat : 0;
    HEAP32[o + 3] = r.seed | 0;
    HEAP32[o + 4] = r.mask;
    HEAP32[o + 5] = r.snap ? r.snap.length : 0;
    return 1;
});
EM_JS(void, js_restore_data, (uint8_t *dst), {
    var r = Module.net.pendingBoot;
    if (r && r.snap)
        HEAPU8.set(r.snap, dst);
});
EM_JS(void, js_ready, (uint32_t f), { Module.net.ready(f >>> 0); });
EM_JS(int, js_golive, (void), { return Module.net.takeGolive(); });
EM_JS(int, js_duty, (void), { return Module.net.duty ? 1 : 0; });
EM_JS(int, js_lead, (void), { return Module.net.lead(); });
EM_JS(int, js_checkpoint_frames, (void), {
    return Module.net.cfg ? Module.net.cfg.checkpointFrames : 240;
});
EM_JS(void, js_event, (int code), {
    if (code === 1) Module.net.ending();
    else if (code === 2) Module.net.finished();
    else if (code === 3) { Module.net.leave(); setTimeout(function () { location.reload(); }, 300); }
});

EM_JS(int, js_boot_record, (uint8_t *dst), {
    var b = Module.net.bootInfo();
    if (!b.rec)
        return 0;
    HEAPU8.set(b.rec, dst);
    return b.rec.length;
});
EM_JS(int, js_game_id, (void), { return Module.net.game ? Module.net.game.id : 0; });
EM_JS(void, js_save, (const char *name, const uint8_t *rec, int len), {
    Module.net.save(UTF8ToString(name), HEAPU8.slice(rec, rec + len));
});
EM_JS(int, js_save_result, (void), { return Module.net.takeSaveResult(); });
EM_JS(int, js_ended, (void), { return Module.net.takeEnded(); });
EM_JS(void, js_submit_score, (const char *name, int score, int parts), {
    Module.net.submitScore(UTF8ToString(name), score, parts);
});
/* The server's table into 7 records of {score i32, parts i32, name char[16]}. */
EM_JS(int, js_scores, (uint8_t *dst), {
    var rows = Module.net.scores;
    if (!rows || rows.length < 7)
        return 0;
    for (var i = 0; i < 7; i++) {
        var o = dst + i * 24;
        HEAP32[o >> 2] = rows[i].score;
        HEAP32[(o + 4) >> 2] = rows[i].parts;
        for (var j = 0; j < 16; j++)
            HEAPU8[o + 8 + j] = j < rows[i].name.length && j < 15 ? rows[i].name.charCodeAt(j) & 127 : 0;
    }
    return 1;
});

EM_JS(void, js_chat_send, (const char *text), { Module.net.chat(UTF8ToString(text)); });
EM_JS(int, js_chat_take, (char *dst, int max), {
    var m = Module.net.takeChat();
    if (!m)
        return -1;
    var n = Math.min(m.text.length, max - 1);
    for (var i = 0; i < n; i++)
        HEAPU8[dst + i] = m.text.charCodeAt(i) & 127;
    HEAPU8[dst + n] = 0;
    return m.seat;
});

EM_JS(int, js_local_input, (uint32_t f), {
    return Module.net.botInput ? Module.net.botInput(f >>> 0) : -1;
});

EM_JS(int, js_command, (void), {
    return Module.net.takeCommand ? Module.net.takeCommand() : 0;
});
EM_JS(int, js_command_arg, (int i), {
    return Module.net.commandArgs ? Module.net.commandArgs[i] | 0 : 0;
});

static int b_boot(start_t *s)
{
    int32_t o[7];
    if (!js_boot(o))
        return 0;
    s->frame = (uint32_t)o[0];
    s->fresh = o[1];
    s->seat = o[2];
    s->seed = (uint32_t)o[3];
    s->mask = o[4];
    s->snaplen = (uint32_t)o[5];
    s->joiner = o[6];
    return 1;
}

static int b_restore(start_t *s)
{
    int32_t o[6];
    if (!js_restore(o))
        return 0;
    s->frame = (uint32_t)o[0];
    s->fresh = o[1];
    s->seat = o[2];
    s->seed = (uint32_t)o[3];
    s->mask = o[4];
    s->snaplen = (uint32_t)o[5];
    return 1;
}

static void b_save(const char *name, const uint8_t *rec, size_t len)
{
    js_save(name, rec, (int)len);
}

static void b_upload(int kind, uint32_t f, uint32_t sum, const uint8_t *data, size_t len)
{
    js_upload(kind, f, sum, data, (int)len);
}

static const backend_t browser = {
    .boot = b_boot, .boot_data = js_boot_data, .send_input = js_send_input, .frame = js_frame,
    .backlog = js_backlog, .wait = js_wait, .sum = js_sum, .upload = b_upload,
    .snap_wanted = js_snap_wanted, .restore = b_restore, .restore_data = js_restore_data,
    .ready = js_ready, .golive = js_golive, .duty = js_duty, .lead = js_lead,
    .checkpoint_frames = js_checkpoint_frames, .event = js_event, .local_input = js_local_input,
    .command = js_command, .command_arg = js_command_arg, .boot_record = js_boot_record,
    .game_id = js_game_id, .save = b_save, .save_result = js_save_result, .ended = js_ended,
    .submit_score = js_submit_score, .scores = (int (*)(net_score_t *))js_scores,
    .chat_send = js_chat_send, .chat_take = js_chat_take,
};
#endif /* NET_BROWSER */

/* ------------------------------------------------------------------ */
/* A frame log (the server's debug option, or a test written by hand): the same game from a
 * file, with nobody to wait for. Lines:
 *   seed N / mask N         how the game starts
 *   F A B [add:S] [remove:S]  sealed frame F: the inputs of seat 0 and 1, then events (S is 1-based)
 *   sum F S HEX L           a checksum a browser reported (ignored here)
 * `--net-restore FILE` starts from a snapshot written by `--net-snapshot F:FILE` instead. */

typedef struct {
    int32_t in[2];
    int nev, kind[MAX_NET_EVENTS], arg[MAX_NET_EVENTS];
    int have;
} logframe_t;

static logframe_t *lf;
static uint32_t lf_count, lf_cap;
static uint32_t lf_seed;
static int lf_mask = 1, lf_seat;
static const char *lf_path, *lf_sums_path, *lf_restore_path;
static uint32_t lf_snap_at = 0xffffffffu;
static const char *lf_snap_path;
static FILE *lf_sums;
static uint8_t *lf_snap;
static uint32_t lf_snaplen, lf_snapframe;
static struct { uint32_t frame; int player, tx, ty, done; } lf_warps[64];
static int lf_nwarps;
static uint32_t lf_parts_at = 0xffffffffu;      /* test aid `parts F JBVW`: set the ship parts at frame F */
static int lf_parts;
/* `record HEX` in a frame log: the game starts from this 92 byte record, like a loaded save. */
static uint8_t lf_record[sizeof(gamestate_t)];
static int lf_record_len;

static int lf_load(void)
{
    FILE *f = fopen(lf_path, "r");
    if (!f) {
        fprintf(stderr, "Cannot read frame log '%s'\n", lf_path);
        return 0;
    }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned a, b, c, d;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "seed %u", &a) == 1) {
            lf_seed = a;
        } else if (sscanf(line, "mask %u", &a) == 1) {
            lf_mask = (int)a;
        } else if (!strncmp(line, "sum ", 4) || !strncmp(line, "desync ", 7)) {
            continue;
        } else if (!strncmp(line, "record ", 7)) {
            size_t n = 0;
            for (const char *h = line + 7; h[0] && h[1] && h[0] != '\n' && n < sizeof lf_record; h += 2) {
                unsigned v;
                if (sscanf(h, "%2x", &v) != 1)
                    break;
                lf_record[n++] = (uint8_t)v;
            }
            lf_record_len = n == sizeof lf_record ? (int)n : 0;
        } else if (sscanf(line, "parts %u %u", &a, &b) == 2) {
            lf_parts_at = a;
            lf_parts = (int)b;
        } else if (sscanf(line, "warp %u %u %u %u", &a, &b, &c, &d) == 4) {
            /* test aid, as in replays: at frame F put player P's Keen on tile X,Y */
            if (lf_nwarps < 64)
                lf_warps[lf_nwarps++] = (typeof(lf_warps[0])){a, (int)b - 1, (int)c, (int)d, 0};
        } else if (sscanf(line, "%u %u %u", &a, &b, &c) == 3) {
            if (a >= lf_cap) {
                uint32_t cap = (a + 1) * 2;
                lf = realloc(lf, cap * sizeof *lf);
                memset(lf + lf_cap, 0, (cap - lf_cap) * sizeof *lf);
                lf_cap = cap;
            }
            logframe_t *fr = &lf[a];
            fr->in[0] = (int32_t)b;
            fr->in[1] = (int32_t)c;
            fr->have = 1;
            /* the events after the two inputs, in the order they were written */
            const char *p = line;
            for (int skip = 0; skip < 3 && p; skip++) {
                p = strchr(p, ' ');
                if (p)
                    p++;
            }
            while (p && *p) {
                int seat;
                if (sscanf(p, "add:%d", &seat) == 1 && fr->nev < MAX_NET_EVENTS) {
                    fr->kind[fr->nev] = EVT_ADD;
                    fr->arg[fr->nev++] = seat - 1;
                } else if (sscanf(p, "remove:%d", &seat) == 1 && fr->nev < MAX_NET_EVENTS) {
                    fr->kind[fr->nev] = EVT_REMOVE;
                    fr->arg[fr->nev++] = seat - 1;
                }
                p = strchr(p, ' ');
                if (p)
                    p++;
            }
            if (a + 1 > lf_count)
                lf_count = a + 1;
        }
    }
    fclose(f);
    return 1;
}

static int l_boot(start_t *s)
{
    if (!lf_load())
        exit(1);
    s->frame = 0;
    s->fresh = 1;
    s->seat = lf_seat;
    s->seed = lf_seed;
    s->mask = lf_mask;
    s->snaplen = 0;
    if (lf_restore_path) {
        FILE *f = fopen(lf_restore_path, "rb");
        uint8_t hd[4];
        if (!f || fread(hd, 1, 4, f) != 4) {
            fprintf(stderr, "Cannot read snapshot '%s'\n", lf_restore_path);
            exit(1);
        }
        lf_snapframe = hd[0] | hd[1] << 8 | hd[2] << 16 | (uint32_t)hd[3] << 24;
        fseek(f, 0, SEEK_END);
        lf_snaplen = (uint32_t)(ftell(f) - 4);
        fseek(f, 4, SEEK_SET);
        lf_snap = malloc(lf_snaplen);
        if (fread(lf_snap, 1, lf_snaplen, f) != lf_snaplen)
            exit(1);
        fclose(f);
        s->frame = lf_snapframe;
        s->fresh = 0;
        s->snaplen = lf_snaplen;
    }
    if (lf_sums_path)
        lf_sums = fopen(lf_sums_path, "w");
    return 1;
}

static void l_boot_data(uint8_t *dst)
{
    memcpy(dst, lf_snap, lf_snaplen);
}

static int l_frame(uint32_t f, sealed_t out)
{
    if (f >= lf_count || !lf[f].have) {
        /* the log is used up: the replay is over */
        if (lf_sums)
            fclose(lf_sums);
        printf("netlog: end at frame %u, score %d, levels done:", f, (int)gs.score);
        for (int i = 0; i < 16; i++)
            if (gs.done[i])
                printf(" %d", i + 1);
        printf("\n");
        fflush(stdout);
        plat_shutdown();
        exit(0);
    }
    const logframe_t *fr = &lf[f];
    out[0] = fr->in[0];
    out[1] = fr->in[1];
    out[2] = fr->nev;
    for (int i = 0; i < fr->nev; i++) {
        out[3 + 2 * i] = fr->kind[i];
        out[4 + 2 * i] = fr->arg[i];
    }
    return 1;
}

static void l_sum(uint32_t f, uint32_t c, int loc, int score)
{
    (void)score;
    if (lf_sums)
        fprintf(lf_sums, "%u %08x %d\n", f, c, loc);
}

static void l_upload(int kind, uint32_t f, uint32_t sum, const uint8_t *data, size_t len)
{
    (void)kind; (void)f; (void)sum; (void)data; (void)len;
}

static int zero(void) { return 0; }
static int two(void) { return 2; }
static void l_wait(uint32_t f) { (void)f; }
static void l_send(uint32_t f, int b) { (void)f; (void)b; }
static void l_ready(uint32_t f) { (void)f; }
static int l_golive(void) { return -1; }
static void l_event(int c)
{
    if (c == NE_ENDING)
        printf("netlog: the ending plays\n");
    if (c == NE_FINISHED) {
        printf("netlog: the game finished at frame %u, levels done:", net_frame);
        for (int i = 0; i < 16; i++)
            if (gs.done[i])
                printf(" %d", i + 1);
        printf("\n");
        fflush(stdout);
        plat_shutdown();
        exit(0);
    }
}
static int l_backlog(uint32_t next) { return lf_count > next ? (int)(lf_count - next) : 0; }
static int l_restore(start_t *s) { (void)s; return 0; }
static void l_restore_data(uint8_t *d) { (void)d; }
static int l_cp(void) { return 240; }
static int l_input(uint32_t f) { (void)f; return -1; }
static int l_command(void) { return 0; }
static int l_command_arg(int i) { (void)i; return 0; }
static int l_game_id(void) { return 1; }
static void l_save(const char *name, const uint8_t *rec, size_t len)
{
    (void)rec;
    printf("netlog: save \"%s\" (%zu bytes)\n", name, len);
}
static int l_save_result(void) { return 1; }
static int l_ended(void) { return -1; }
static void l_submit_score(const char *name, int32_t score, int parts)
{
    printf("netlog: high score %d \"%s\" parts %d\n", (int)score, name, parts);
}
static int l_scores(net_score_t *rows) { (void)rows; return 0; }
static void l_chat_send(const char *text) { (void)text; }
static int l_chat_take(char *text, int max) { (void)text; (void)max; return -1; }

static int l_boot_record(uint8_t *dst)
{
    memcpy(dst, lf_record, (size_t)lf_record_len);
    return lf_record_len;
}

static const backend_t logfile __attribute__((unused)) = {
    .boot = l_boot, .boot_data = l_boot_data, .send_input = l_send, .frame = l_frame,
    .backlog = l_backlog, .wait = l_wait, .sum = l_sum, .upload = l_upload, .snap_wanted = zero,
    .restore = l_restore, .restore_data = l_restore_data, .ready = l_ready, .golive = l_golive,
    .duty = zero, .lead = two, .checkpoint_frames = l_cp, .event = l_event, .local_input = l_input,
    .command = l_command, .command_arg = l_command_arg, .boot_record = l_boot_record,
    .game_id = l_game_id, .save = l_save, .save_result = l_save_result, .ended = l_ended,
    .submit_score = l_submit_score, .scores = l_scores, .chat_send = l_chat_send,
    .chat_take = l_chat_take,
};

/* Command-line options of the log backend (main.c). */
int net_log_option(const char *opt, const char *arg)
{
    if (!strcmp(opt, "--netlog"))
        lf_path = arg;
    else if (!strcmp(opt, "--seat"))
        lf_seat = atoi(arg) - 1 < 0 ? 0 : atoi(arg) - 1;
    else if (!strcmp(opt, "--netsums"))
        lf_sums_path = arg;
    else if (!strcmp(opt, "--net-restore"))
        lf_restore_path = arg;
    else if (!strcmp(opt, "--net-snapshot")) {
        /* FRAME:FILE */
        lf_snap_at = (uint32_t)strtoul(arg, NULL, 0);
        const char *colon = strchr(arg, ':');
        lf_snap_path = colon ? colon + 1 : NULL;
    } else
        return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The game's side */

static const backend_t *be;
static uint32_t next_send = 0xffffffffu;    /* the next frame to send our input for; none until we are live */
static uint32_t reported = 0xffffffffu;     /* the frame whose checksum went out last */
static int segment_first;           /* the next frame is the first of a map phase or level */
static start_t restore_info;        /* a snapshot waiting for the game to restart its segment from it */
static uint8_t *restore_buf;
static int restore_waiting;
static int want_ready;              /* a joiner tells the server once it is level with the stream */
static uint8_t start_record[sizeof(gamestate_t)];  /* the save a loaded game starts from */
static int start_record_len;

#define FAST_BACKLOG 12             /* more sealed frames waiting than this: we are catching up */

int net_have_backend(void)
{
#ifdef NET_BROWSER
    return 1;
#else
    return lf_path != NULL;
#endif
}

/* Reads how the game starts and sets the game up for it. */
int net_boot(void)
{
#ifdef NET_BROWSER
    be = &browser;
#else
    be = &logfile;
#endif
    start_t s;
    while (!be->boot(&s)) {
        plat_pump();
        plat_sleep(20);
    }
    net_seat = s.seat;
    local_player = s.seat;
    nplayers = 2;
    game.coop = 1;
    game_seed = s.seed;
    game.present = s.mask;
    for (int i = 0; i < MAXPLAYERS; i++)
        players[i].input = IN_NET;
    net_frame = s.frame;
    start_record_len = be->boot_record(start_record);
    /* A player who creates the game sends from frame 0; one who joins sends once the server
     * says `golive`. */
    next_send = s.joiner ? 0xffffffffu : 0;
    want_ready = s.joiner;
    if (s.snaplen) {
        restore_info = s;
        restore_buf = malloc(s.snaplen);
        be->boot_data(restore_buf);
        restore_waiting = 1;
    }
    return 1;
}

/* A snapshot may have arrived while we play (the server repairs a browser that drifted). */
static int poll_restore(void)
{
    if (restore_waiting)
        return 1;
    start_t s;
    if (!be->restore(&s) || !s.snaplen)
        return 0;
    free(restore_buf);
    restore_info = s;
    restore_buf = malloc(s.snaplen);
    be->restore_data(restore_buf);
    restore_waiting = 1;
    return 1;
}

/* The kind of segment the waiting snapshot was taken in: 0 when there is none, 1 the world
 * map, 2 a level (`*level`). */
int net_resume(int *level)
{
    if (!poll_restore())
        return 0;
    int in_map;
    if (!sim_snapshot_kind(restore_buf, restore_info.snaplen, &in_map, level)) {
        fprintf(stderr, "net: not a snapshot\n");
        restore_waiting = 0;
        return 0;
    }
    return in_map ? 1 : 2;
}

/* The segment has been set up and its first frame is next: load the waiting snapshot into it.
 * Returns 1 when there was one. */
int net_restore_apply(void)
{
    if (!restore_waiting)
        return 0;
    restore_waiting = 0;
    int ok = sim_restore(restore_buf, restore_info.snaplen);
    free(restore_buf);
    restore_buf = NULL;
    if (!ok) {
        fprintf(stderr, "net: the snapshot did not load\n");
        return 0;
    }
    net_frame = restore_info.frame;
    reported = 0xffffffffu;
    segment_first = 0;
    draw_camera();
    return 1;
}

/* A map phase or level has been set up and its first frame is next. */
void net_segment_begin(void)
{
    segment_first = 1;
    reported = 0xffffffffu;
}

static ctrl_t read_local(void)
{
    ctrl_t c;
    read_ctrl(&c);
    c.ev = 0;
    ui_filter_ctrl(local_player, &c);
    return c;
}

/* Our input for the frames that are due. */
static void send_inputs(void)
{
    if (next_send == 0xffffffffu)
        return;
    uint32_t lead = (uint32_t)be->lead();
    while (next_send <= net_frame + lead) {
        int b = be->local_input(next_send);
        if (b < 0) {
            ctrl_t c = read_local();
            b = pack_ctrl(&c);
        }
        be->send_input(next_send, b);
        next_send++;
    }
}

/* The checksum of the state before frame net_frame, and the checkpoint if it is due. */
static void report(void)
{
    if (reported == net_frame)
        return;
    if (!segment_first && net_frame % 24 != 0)
        return;
    reported = net_frame;
    uint32_t sum = sim_checksum();
    be->sum(net_frame, sum, in_world_map ? 0 : cur_level, (int)gs.score);
    if (be->duty() && !net_fast &&
        (segment_first || net_frame % (uint32_t)be->checkpoint_frames() == 0)) {
        uint8_t *buf;
        size_t n = sim_save(&buf);
        be->upload(UP_CHECKPOINT, net_frame, sum, buf, n);
        free(buf);
    }
    segment_first = 0;
}

/* The server asked for the state as it is now, because another browser drifted. */
static void answer_snap_request(void)
{
    if (!be->snap_wanted())
        return;
    uint8_t *buf;
    size_t n = sim_save(&buf);
    be->upload(UP_RESYNC, net_frame, sim_checksum(), buf, n);
    free(buf);
}

static void apply_frame(const sealed_t f)
{
    for (int i = 0; i < f[2]; i++) {
        int kind = f[3 + 2 * i], arg = f[4 + 2 * i];
        if (kind == EVT_ADD)
            coop_add_player(arg);
        else if (kind == EVT_REMOVE)
            coop_remove_player(arg);
    }
    for (int p = 0; p < nplayers; p++) {
        ctrl_t c = {8, 0, 0, 0};
        if (PRESENT(p)) {
            int b = f[p];
            c = (ctrl_t){(int16_t)IN_DIR(b), (int16_t)IN_B1(b), (int16_t)IN_B2(b), (int16_t)IN_EV(b)};
        }
        players[p].ctrl = c;
    }
}

/* Test aid of the log backend: a snapshot written when the game reaches a frame. */
static void test_hooks(void)
{
    if (net_frame == lf_parts_at) {
        gs.joystick = lf_parts / 1000 % 10;
        gs.battery = lf_parts / 100 % 10;
        gs.vacuum = lf_parts / 10 % 10;
        gs.whiskey = lf_parts % 10;
        lf_parts_at = 0xffffffffu;
    }
    for (int i = 0; i < lf_nwarps; i++)
        if (!lf_warps[i].done && lf_warps[i].frame == net_frame) {
            lf_warps[i].done = 1;
            sim_warp(lf_warps[i].player, lf_warps[i].tx, lf_warps[i].ty);
        }
    if (net_frame != lf_snap_at || !lf_snap_path)
        return;
    lf_snap_at = 0xffffffffu;
    FILE *f = fopen(lf_snap_path, "wb");
    uint8_t *buf;
    size_t n = sim_save(&buf);
    uint8_t hd[4] = {(uint8_t)net_frame, (uint8_t)(net_frame >> 8), (uint8_t)(net_frame >> 16),
                     (uint8_t)(net_frame >> 24)};
    if (f) {
        fwrite(hd, 1, 4, f);
        fwrite(buf, 1, n, f);
        fclose(f);
    }
    free(buf);
}

extern uint16_t rng_state[18];

/* Test commands (from the bot harness, or typed into the page's console): 1 changes the game
 * state behind the other players' backs (so the next checksum differs), 2 does it to the position of
 * the first Keen. 3 and 4 are test aids like the replay file's `warp` and `parts`: with the same
 * arguments on every machine they change the game on the same frame everywhere, so nothing
 * drifts. 3: warp (frame, player, tile x, tile y) puts a Keen on a tile; 4: parts (frame, JBVW as
 * four digits) sets the ship parts. */
#define MAX_SCHEDULED 8
static struct { uint32_t frame; int player, tx, ty, parts, kind; } scheduled[MAX_SCHEDULED];

static int next_slot(void)
{
    for (int i = 0; i < MAX_SCHEDULED; i++)
        if (!scheduled[i].kind)
            return i;
    return 0;
}

static void run_command(void)
{
    switch (be->command()) {
    case 1:
        rng_state[3] ^= 1;
        break;
    case 2:
        for (int i = 0; i < nplayers; i++)
            if (objs[i].type) {
                objs[i].x += 0x1000;
                break;
            }
        break;
    case 3: {
        int i = next_slot();
        scheduled[i].kind = 3;
        scheduled[i].frame = (uint32_t)be->command_arg(0);
        scheduled[i].player = be->command_arg(1);
        scheduled[i].tx = be->command_arg(2);
        scheduled[i].ty = be->command_arg(3);
        break;
    }
    case 4: {
        int i = next_slot();
        scheduled[i].kind = 4;
        scheduled[i].frame = (uint32_t)be->command_arg(0);
        scheduled[i].parts = be->command_arg(1);
        break;
    }
    }
    for (int i = 0; i < MAX_SCHEDULED; i++) {
        if (!scheduled[i].kind || net_frame != scheduled[i].frame)
            continue;
        if (scheduled[i].kind == 3) {
            sim_warp(scheduled[i].player, scheduled[i].tx, scheduled[i].ty);
        } else {
            gs.joystick = scheduled[i].parts / 1000 % 10;
            gs.battery = scheduled[i].parts / 100 % 10;
            gs.vacuum = scheduled[i].parts / 10 % 10;
            gs.whiskey = scheduled[i].parts % 10;
        }
        scheduled[i].kind = 0;
    }
}

/* Called at the top of every simulated frame, in the map phase and in levels, before the
 * frame's own work. Sends our input, waits for the sealed frame and applies it. Returns 0 to
 * go on, or 1 when a snapshot arrived and the segment has to be restarted from it (the caller
 * returns up to play_game). */
int net_frame_begin(void)
{
    static uint32_t ff_count;
    for (;;) {
        if (poll_restore()) {
            plat_set_fast(0);
            return 1;
        }
        test_hooks();
        run_command();
        answer_snap_request();
        int golive = be->golive();
        if (golive >= 0)
            next_send = (uint32_t)golive;
        report();
        send_inputs();
        sealed_t f;
        if (be->frame(net_frame, f)) {
            apply_frame(f);
            break;
        }
        be->wait(net_frame);
        plat_pump();
        plat_sleep(2);
    }
    /* A long way behind the stream (a joiner, or after a restore): no drawing, no sound and
     * no pacing until level with it. A joiner then tells the server it has caught up. */
    int fast = be->backlog(net_frame + 1) > FAST_BACKLOG;
    net_fast = fast;
    plat_set_fast(fast);
    if (fast) {
        if ((++ff_count & 15) == 0) {
            plat_pump();
            plat_sleep(0);         /* let the browser deliver messages and draw now and then */
        }
    } else if (want_ready) {
        want_ready = 0;
        be->ready(net_frame);
    }
    net_frame++;
    return 0;
}

/* While a blocking screen is up (help or key setup on the world map) this machine runs no frames,
 * but the other player's game does: it needs a neutral input from us for every frame, at the
 * pace of the game, never more than `lead` frames ahead of what has been sealed. When the
 * screen closes the sealed frames wait for us and the game catches up in fast mode. */
static uint32_t idle_ticks;

static void idle_pump(void)
{
    if (next_send == 0xffffffffu)
        return;
    uint32_t now = plat_ticks();
    if (now - idle_ticks < SIM_TICS)
        return;
    idle_ticks = now;
    uint32_t sealed_to = net_frame + (uint32_t)be->backlog(net_frame);     /* one past the last sealed */
    if (next_send > sealed_to + (uint32_t)be->lead())
        return;
    be->send_input(next_send++, 8);
}

void net_local_screen(int on)
{
    idle_ticks = plat_ticks();
    plat_idle_hook = on ? idle_pump : NULL;
}

/* The ending plays and the game finishes, or the player leaves. */
void net_event(int code)
{
    if (!be)
        return;
    if (code == NE_ENDING) {
        /* one last report, so the score the table may take is the one the game ends with */
        be->sum(net_frame, sim_checksum(), in_world_map ? 0 : cur_level, (int)gs.score);
    }
    be->event(code);
}

/* The record of a game loaded from a save, or 0 for a new game. */
int net_start_record(void *out)
{
    if (start_record_len != (int)sizeof(gamestate_t))
        return 0;
    memcpy(out, start_record, sizeof start_record);
    return 1;
}

int net_game_id(void)
{
    return be ? be->game_id() : 0;
}

/* F5 on the world map: stores the shared record on the server and waits for its answer, up to
 * five seconds. The input stream goes on meanwhile (net_local_screen). 1 when it was stored. */
int net_save_record(const char *name)
{
    if (!be)
        return 0;
    be->save(name, (const uint8_t *)&gs, sizeof gs);
    for (int i = 0; i < 350; i++) {
        int r = be->save_result();
        if (r >= 0)
            return r;
        plat_vsync(1);
    }
    return 0;
}

/* After the ending: waits for the server to say who types the team name (that seat), or -1. */
int net_wait_ended(void)
{
    int v;
    while ((v = be->ended()) == -2) {
        plat_pump();
        plat_sleep(20);
    }
    return v;
}

void net_submit_score(const char *name, int32_t score, int parts)
{
    be->submit_score(name, score, parts);
}

int net_scores(net_score_t *rows)
{
    return be && be->scores(rows);
}

/* Chat goes through the server beside the frames: it is not game state. */
void net_chat_send(const char *text)
{
    if (be)
        be->chat_send(text);
}

int net_chat_take(char *text, int max)
{
    return be ? be->chat_take(text, max) : -1;
}

/* Nothing more to do: the page returns to the lobby by itself. */
void net_idle_forever(void)
{
    plat_set_fast(0);
    for (;;) {
        plat_pump();
        plat_sleep(100);
    }
}
