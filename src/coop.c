/* Co-op rules (openspec change coop-game-rules): player status, level success
 * and failure, votes, rollback of the shared record, timed banners.
 *
 * Everything in `game` is game state. Inputs reach it only as the one-frame
 * event bits carried in each player's ctrl, so two machines running the same
 * inputs reach the same votes on the same frame. */
#include <string.h>

#include "game.h"

game_t game = {.req_by = -1};

static int all_players(void)
{
    return (1 << nplayers) - 1;
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
    for (int i = 0; i < nplayers; i++) {
        players[i].status = PS_ALIVE;
        players[i].tele = TP_NONE;
    }
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
    for (int i = 0; i < nplayers; i++) {
        players[i].status = PS_ALIVE;
        players[i].tele = TP_NONE;
    }
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
    if (game.restart_votes == all_players())
        return LR_FAIL;
    if (game.map_votes == all_players())
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
        if (players[p].status != PS_ALIVE) {
            players[p].cam_x = players[target].cam_x;
            players[p].cam_y = players[target].cam_y;
        }
}
