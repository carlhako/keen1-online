/* Co-op rules (openspec change coop-game-rules): player status, level success
 * and failure, votes, rollback of the shared record, timed banners.
 *
 * Everything in `game` is game state. Inputs reach it only as the one-frame
 * event bits carried in each player's ctrl, so two machines running the same
 * inputs reach the same votes on the same frame. */
#include <string.h>

#include "game.h"
#include "kdata.h"

game_t game = {.req_by = -1, .present = 1};

/* The players whose Keens are in the game: a vote or a start request needs every one of them. */
static int all_players(void)
{
    return game.present;
}

/* Everybody present starts alive; an empty seat has no status. */
static void reset_statuses(void)
{
    for (int i = 0; i < nplayers; i++) {
        players[i].status = PRESENT(i) ? PS_ALIVE : PS_ABSENT;
        players[i].tele = TP_NONE;
    }
}

/* A level starts: nobody has voted, no message is up, everybody is alive, and
 * the shared record is kept so a failure can roll back to it. */
void coop_level_begin(void)
{
    game.restart_votes = game.map_votes = game.pause_mask = 0;
    game.req_by = -1;               /* the level-start request has done its job */
    game.req_level = 0;
    game.req_mask = 0;
    game.shake = 0;
    memset(game.banner, 0, sizeof game.banner);
    game.start_gs = gs;
    reset_statuses();
}

/* The level failed or was left: the shared record goes back to how it was at
 * the start (score, ammo, keys, ship parts, pogo, completed levels). */
void coop_rollback(void)
{
    gs = game.start_gs;
    last_extra = gs.score / 20000 * 20000;
}

/* The map starts or comes back after a level: nothing pending. */
void coop_map_begin(void)
{
    game.restart_votes = game.map_votes = game.pause_mask = 0;
    game.req_by = -1;
    game.req_level = 0;
    game.req_mask = 0;
    game.shake = 0;
    memset(game.banner, 0, sizeof game.banner);
    reset_statuses();
}

/* This frame's one-frame events. Votes count in a level; the pause bits are for
 * the world map (a level never pauses). */
void coop_events(int in_level)
{
    for (int p = 0; p < nplayers; p++) {
        int ev = players[p].ctrl.ev, bit = 1 << p;
        if (in_level) {
            if (ev & EV_RESTART_TOGGLE)
                game.restart_votes ^= bit;
            if (ev & EV_MAP_TOGGLE)
                game.map_votes ^= bit;
        } else {
            if (ev & EV_PAUSE_ON)
                game.pause_mask |= bit;
            if (ev & EV_PAUSE_OFF)
                game.pause_mask &= ~bit;
        }
    }
}

void banner_add(int kind)
{
    int free_slot = -1;
    for (int i = 0; i < MAXBANNERS; i++) {
        if (game.banner[i].kind == kind) {
            free_slot = i;
            break;
        }
        if (free_slot < 0 && game.banner[i].kind == BN_NONE)
            free_slot = i;
    }
    if (free_slot < 0)
        free_slot = 0;
    game.banner[free_slot].kind = kind;
    game.banner[free_slot].frames = BANNER_FRAMES;
}

/* Once per simulated frame. */
void coop_tick(void)
{
    for (int i = 0; i < MAXBANNERS; i++)
        if (game.banner[i].kind != BN_NONE && --game.banner[i].frames <= 0)
            game.banner[i].kind = BN_NONE;
    if (game.shake > 0 && --game.shake == 0)
        level_result = LR_CASTLE;
}

/* After each frame: has the level ended? Returns an LR_* code, or -1 to go on.
 * Nobody alive and somebody exited is a success; nobody alive otherwise a
 * failure; the teleporter and the castle switch end it for everyone. */
int coop_level_eval(void)
{
    if (level_result)
        return level_result;
    int alive = 0, exited = 0;
    for (int p = 0; p < nplayers; p++) {
        alive += players[p].status == PS_ALIVE;
        exited += players[p].status == PS_EXITED;
    }
    if (!alive)
        return exited ? LR_DONE : LR_FAIL;
    if (game.restart_votes && game.restart_votes == all_players())
        return LR_FAIL;
    if (game.map_votes && game.map_votes == all_players())
        return LR_VOTE_LEAVE;
    return -1;
}

/* Dead and exited players watch the lowest-numbered player still alive. */
void coop_follow_cameras(void)
{
    int target = -1;
    for (int p = 0; p < nplayers && target < 0; p++)
        if (players[p].status == PS_ALIVE)
            target = p;
    if (target < 0)
        return;
    for (int p = 0; p < nplayers; p++)
        if (players[p].status != PS_ALIVE && players[p].status != PS_ABSENT) {
            players[p].cam_x = players[target].cam_x;
            players[p].cam_y = players[target].cam_y;
        }
}

/* ------------------------------------------------------------------ */
/* Online: players join and leave a running game. Both happen on a frame every
 * browser agrees on (the server seals them into the frame stream), so every
 * machine changes the game the same way. */

extern void keen_walk(void), keen_touch(obj_t *, obj_t *);

/* Where the Keen of a joining player goes: on top of the lowest-numbered Keen that is
 * alive, else any Keen in the game, else where the level starts (or the map's entrance). */
static void join_position(int seat, int32_t *x, int32_t *y)
{
    int ref = -1;
    for (int pass = 0; pass < 2 && ref < 0; pass++)
        for (int p = 0; p < nplayers && ref < 0; p++)
            if (p != seat && PRESENT(p) && objs[p].type == OT_KEEN &&
                (pass == 1 || players[p].status == PS_ALIVE))
                ref = p;
    if (ref >= 0) {
        *x = objs[ref].x;
        *y = objs[ref].y;
        return;
    }
    *x = gs.mapx;
    *y = gs.mapy;
    if (!in_world_map)
        for (int ty = 0; ty < map_h; ty++)
            for (int tx = 0; tx < map_w; tx++)
                if (MAPI(tx, ty) == 0xff) {
                    *x = (int32_t)tx << 12;
                    *y = ((int32_t)ty << 12) + 0x800;
                    return;
                }
}

void coop_add_player(int seat)
{
    if (seat < 0 || seat >= nplayers)
        return;
    int32_t x, y;
    join_position(seat, &x, &y);
    player_t *pl = &players[seat];
    obj_t *k = &objs[seat];
    game.present |= 1 << seat;
    memset(k, 0, sizeof *k);
    k->type = OT_KEEN;
    k->active = 1;
    k->x = x;
    k->y = y;
    pl->ctrl = pl->ctrl_prev = (ctrl_t){8, 0, 0, 0};
    pl->status = PS_ALIVE;
    pl->tele = TP_NONE;
    pl->invuln = 0;
    pl->facing = 1;
    if (in_world_map) {
        k->think = think_none;
        k->touch = touch_none;
        k->sprite = 0x24;
        pl->map_x = x;
        pl->map_y = y;
        map_camera_center(seat);
    } else {
        k->think = keen_walk;
        k->touch = keen_touch;
        k->sprite = 4;
        camera_start(seat);
    }
}

void coop_remove_player(int seat)
{
    if (seat < 0 || seat >= nplayers)
        return;
    int bit = 1 << seat;
    game.present &= ~bit;
    memset(&objs[seat], 0, sizeof objs[seat]);
    players[seat].status = PS_ABSENT;
    players[seat].tele = TP_NONE;
    players[seat].ctrl = players[seat].ctrl_prev = (ctrl_t){8, 0, 0, 0};
    game.restart_votes &= ~bit;
    game.map_votes &= ~bit;
    game.pause_mask &= ~bit;
    game.req_mask &= ~bit;
    if (game.req_by == seat) {
        game.req_by = -1;
        game.req_level = 0;
        game.req_mask = 0;
    }
}
