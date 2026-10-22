/* Commander Keen 1 game logic, reconstructed from KEEN1.EXE (v1.1). */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* Object: same layout and meaning as the original 0x36 byte records. */
typedef struct obj obj_t;
typedef void (*think_t)(void);
typedef void (*touch_t)(obj_t *self, obj_t *other);

enum {
    OT_NONE, OT_KEEN, OT_YORP, OT_GARG, OT_VORT, OT_BUTLER, OT_TANK, OT_7, OT_CHAIN,
    OT_9, OT_KEENSHOT, OT_ENEMYSHOT, OT_ICECHUNK, OT_SPLASH, OT_DEAD, OT_ICEBLOCK
};

struct obj {
    int16_t type;            /* 0x00 */
    int16_t active;          /* 0x02 on screen and thinking */
    int32_t x, y;            /* 0x04 position, 1/256 pixel */
    int32_t x1, y1, x2, y2;  /* 0x0c hitbox in world coordinates */
    int16_t dx, dy;          /* 0x1c movement this frame */
    int16_t xv, yv;          /* 0x20 velocity per tick */
    int16_t w24, w26;        /* 0x24 health / base sprite */
    int16_t sprite;          /* 0x28 */
    int16_t timer;           /* 0x2a */
    int16_t w2c, w2e;        /* 0x2c work values */
    int16_t w30;             /* 0x30 */
    think_t think;           /* 0x32 */
    touch_t touch;           /* 0x34 */
};

/* "Special" level objects (doors, bridges, cannons, ...), 0x24 bytes. */
typedef struct spec spec_t;
struct spec {
    int32_t x, y;            /* 0x00 tile coordinates (or world for doors) */
    int16_t active;          /* 0x08 */
    int16_t cnt;             /* 0x0a */
    int16_t w6, w7, w8;      /* 0x0c 0x0e 0x10 */
    void (*think)(spec_t *); /* 0x22 */
};

#define MAXOBJS 60
#define MAXSPECS 16

/* Saved game state: identical to the 92 byte SAVEDn.CK1 files (DS:A5C0). */
#pragma pack(push, 1)
typedef struct {
    int16_t joystick;        /* ship parts */
    int16_t vacuum;
    int16_t whiskey;
    int16_t pogo;
    int16_t battery;
    int16_t keys[4];         /* yellow, red, green, blue keycards */
    int16_t done[16];        /* levels 1..16 completed */
    int16_t lives;
    int16_t ammo;
    int32_t score;
    int32_t mapx, mapy;      /* Keen on the world map */
    int32_t mapsx, mapsy;    /* world map scroll */
    int16_t unused[8];
    int16_t unused2;
} gamestate_t;
#pragma pack(pop)

extern gamestate_t gs;

/* Controls (DS:79C2): direction 0..7 clockwise from up, 8 = none. `ev` holds
 * one-frame events that travel with the input (votes, pauses), not held keys. */
typedef struct {
    int16_t dir, b1, b2;
    int16_t ev;
} ctrl_t;

enum {
    EV_RESTART_TOGGLE = 1,      /* vote to restart the level, or withdraw the vote */
    EV_MAP_TOGGLE = 2,          /* vote to return to the world map, or withdraw it */
    EV_PAUSE_ON = 4,            /* this player opened help or key setup on the map */
    EV_PAUSE_OFF = 8,           /* ...and closed it again */
    EV_SAVE_ON = 16,            /* this player opened the save prompt on the map (online) */
};

/* One player. Keens occupy object slots 0..nplayers-1 (slot i is player i),
 * so "Keen thinks first and is drawn on top" still holds. Everything here
 * except `input` is game state: it goes into snapshots and checksums. */
#define MAXPLAYERS 4
enum { IN_KEYS1, IN_KEYS2, IN_REPLAY, IN_NET }; /* where a player's input comes from */
enum { PS_ALIVE, PS_DEAD, PS_EXITED, PS_ABSENT }; /* a player's status in a co-op level; absent: nobody in that seat */
enum { TP_NONE, TP_FROM, TP_TO, TP_WAIT }; /* world-map teleport: leaving, arriving, buttons up */
typedef struct {
    int32_t facing;                 /* was keen_facing */
    int32_t invuln;                 /* was keen_invuln */
    ctrl_t ctrl, ctrl_prev;         /* this frame's and last frame's input */
    int32_t cam_x, cam_y;           /* camera (level or map), 1/256 pixel */
    int32_t cam_tx, cam_ty;         /* camera in tiles, as seen by the last frame */
    int32_t map_x, map_y;           /* world map position */
    int32_t on_switch, switch_x, switch_y;
    int32_t status;                 /* PS_*, in a co-op level */
    int32_t tele, tele_t;           /* TP_* and the frame counter of the phase */
    int32_t tele_tx, tele_ty, tele_flag;    /* tile and animation set being played */
    int32_t tele_x, tele_y, tele_dflag;     /* where the Keen arrives, and its animation set */
    int32_t input;                  /* IN_*; not part of the game state */
} player_t;

extern player_t players[MAXPLAYERS];
extern int nplayers;
extern int local_player;            /* whose camera is drawn */
extern player_t *cur_plr;           /* the player whose Keen is thinking */
extern uint32_t gtick;              /* game time in ticks; replaces the wall clock */
extern uint32_t frame_no;           /* simulated frames since the level (or map) started */

/* Co-op rules (coop.c). Everything here is game state: snapshots and checksums
 * include it. */
#define MAXBANNERS 4
enum { BN_NONE, BN_STATUE, BN_CASTLE };     /* timed banners; vote and request banners are derived */
typedef struct {
    int32_t kind, frames;
} banner_t;

typedef struct {
    int32_t coop;                   /* co-op rules on: set by the lobby, --local2 and --coop */
    int32_t present;                /* bit per player whose Keen is in the game (online: a seat can be empty) */
    int32_t levels_started;         /* levels begun in this game; seeds each level's random generator */
    int32_t restart_votes;          /* bit per player */
    int32_t map_votes;              /* bit per player: return to the world map */
    int32_t pause_mask;             /* bit per player holding help or key setup open; bit 4+p: ...saving */
    int32_t req_by;                 /* world-map start request: requesting player, -1 none */
    int32_t req_level;
    int32_t req_mask;               /* players who confirmed it */
    int32_t entry_x, entry_y;       /* where Keens appear on the map after a level */
    int32_t shake;                  /* castle switch: frames left until the level ends */
    banner_t banner[MAXBANNERS];
    gamestate_t start_gs;           /* the shared record when the level started */
} game_t;
extern game_t game;
#define PRESENT(p) ((game.present >> (p)) & 1)

#define BANNER_FRAMES 120               /* about five seconds */
void coop_level_begin(void);
void coop_rollback(void);
void coop_map_begin(void);
void coop_events(int in_level);
void banner_add(int kind);
void coop_tick(void);
int coop_level_eval(void);              /* LR_* or -1 */
void coop_follow_cameras(void);
int coop_level_start(int32_t *x, int32_t *y);   /* the level's start marker: 1 found */
int coop_anyone_exited(void);
void coop_add_player(int seat);         /* online: a player joins; the Keen appears on top of another */
void coop_remove_player(int seat);      /* online: a player's Keen leaves the game */

/* How play_level ends. */
enum {
    LR_DIED,            /* non-co-op: Keen died */
    LR_DONE,            /* exit door */
    LR_TELEPORT,        /* in-level teleporter */
    LR_CASTLE,          /* castle switch */
    LR_FAIL,            /* co-op: every player dead, or all voted to restart */
    LR_VOTE_LEAVE,      /* co-op: all voted to return to the map */
    LR_STOPPED,         /* replay: the recorded frames are used up */
    LR_RESTORE,         /* online: a snapshot arrived; play_game restarts the segment from it */
};

/* Engine state */
extern int tics;
extern obj_t objs[MAXOBJS], cur;
extern int numobjs;
extern spec_t specs[MAXSPECS];
extern int numspecs;
extern int cur_level;
extern int32_t scrollmin_x, scrollmin_y, scrollmax_x, scrollmax_y;
extern int32_t objmax_x, objmax_y, keenmax_x, keendeath_y;
extern int lights_on;
extern int level_result, godmode, abort_game, warp_level;
extern int32_t last_extra;
extern int in_world_map;

/* Fixed simulation step: 6 ticks (timer rate 1193182/8192 Hz), about 24.3 frames/s. */
#define SIM_TICS 6

/* engine.c */
void frame_start(void);
void frame_refresh(void);
void load_level(int num);
int play_level(int num);
void read_ctrl(ctrl_t *c);                          /* key set 1, for menus */
void read_player_ctrl(int p);                       /* this frame's input of player p */
void rng_init(void);
void rng_seed(uint32_t seed);
obj_t *nearest_keen(void);                          /* nearest living Keen to cur */
int player_of(const obj_t *o);                      /* player number of a Keen object, or -1 */
void camera_start(int p);                           /* centre player p's camera on his Keen */
void map_camera_center(int p);                      /* the same on the world map (menus.c) */
void draw_keen(int32_t x, int32_t y, int sprite, int slot);
int cam_sees(int tx, int ty, int l, int t, int r, int b);
int tile_at(int tx, int ty);
void set_tile(int tx, int ty, int t);
obj_t *new_obj(void);
spec_t *new_spec(void);
void calc_hitbox(void);
void calc_hitbox_obj(obj_t *o);
void accel_x(int a);
void gravity(void);
int move_obj(void);
int clip_move(void);
void add_score(int pts);
void kill_keen(obj_t *k);
int rand8(void);
int rand_range(int max);
void think_none(void);
void touch_none(obj_t *self, obj_t *other);
void keen_walk(void);
void keen_touch(obj_t *self, obj_t *other);
void think_splash(void);
void think_dying(void);
void think_dead(void);
int check_hotkeys(void);
void draw_camera(void);                             /* the local player's camera, to the renderer */
void check_cheats(void);

/* Think/touch/special function ids, so snapshots do not hold pointers (sim.c). */
int think_id(think_t f);
think_t think_from_id(int id);
int touch_id(touch_t f);
touch_t touch_from_id(int id);
int spec_think_id(void (*f)(spec_t *));
void (*spec_think_from_id(int id))(spec_t *);

/* sim.c: snapshots, checksums, replay */
size_t sim_save(uint8_t **buf);                     /* *buf is owned by sim.c */
int sim_restore(const uint8_t *buf, size_t len);
uint32_t sim_checksum(void);
extern int replaying;                               /* running from a recording */
int replay_load(const char *path);
void replay_apply(void);                            /* set this frame's inputs */
void sim_apply_script_warps(void);                  /* live runs: warps queued by KEEN_SCRIPT */
int replay_level_done(void);                        /* recorded frames used up */
int replay_is_coop(void);
int replay_is_flow(void);                           /* the replay drives play_game (map and levels) */
int replay_players(void);
int replay_first_level(void);
int replay_next_segment(int is_map, int level);     /* co-op replay: the next map or level */
void replay_report(int level, int r);
int checksums_open(const char *path);
int replay_run(int selftest);
void replay_frame_end(void);                        /* per-frame hook: checksum, selftest */
void record_open(const char *path, int level);
void record_level_begin(int level, uint32_t seed);
void record_level_end(void);
void record_map_begin(void);
void record_map_end(void);
extern uint32_t game_seed;
void record_frame(void);
void record_close(void);
extern int headless_delay_ms;
void sim_dump_at_exit(void);

/* net.c: online play. The game runs the frames a server seals, with everybody's inputs. */
extern int net_online;                  /* --online: the lobby started this game */
extern int net_seat;                    /* this machine's player */
extern uint32_t net_frame;              /* the next frame to simulate, counted across map and levels */
int net_log_option(const char *opt, const char *arg);   /* --netlog and friends (native tests) */
int net_have_backend(void);
int net_boot(void);                     /* read how the game starts; sets players, seed, who is present */
int net_resume(int *level);             /* 0 nothing to restore, 1 into the map, 2 into level *level */
int net_restore_apply(void);            /* a segment is set up: load the waiting snapshot into it */
void net_segment_begin(void);           /* a map phase or level starts: report, maybe checkpoint */
int net_frame_begin(void);              /* top of a simulated frame; 1 = restart the segment from a snapshot */
void net_event(int code);               /* 1 the ending starts, 2 the game finished, 3 leave */
void net_local_screen(int on);          /* a blocking local screen is up: keep the input stream going */
void net_idle_forever(void);
int net_start_record(void *out);        /* a game loaded from a save: its 92 byte record; 0 for a new game */
int net_game_id(void);
int net_save_record(const char *name);  /* store the shared record on the server: 1 stored, 0 failed */
int net_wait_ended(void);               /* after the ending: the seat to type the team name, or -1 */
void net_submit_score(const char *name, int32_t score, int parts);
typedef struct { int32_t score, parts; char name[16]; } net_score_t;
int net_scores(net_score_t *rows);      /* the server's high score table (7 rows); 0 when not known */
void net_chat_send(const char *text);   /* a chat line to everybody in the game */
int net_chat_take(char *text, int max); /* a chat line that arrived (the sender's too): its seat, or -1 */
void sim_warp(int p, int tx, int ty);        /* test aid: put a Keen on a tile */
int sim_snapshot_kind(const uint8_t *buf, size_t len, int *in_map, int *level);

/* actors.c */
void spawn_actors(void);
void yorp_look(void);
void yorp_stunned(void);
void spawn_tankshot(int32_t x, int32_t y, int xv);
void spec_door(spec_t *s);
void spec_bridge_extend(spec_t *s);
void spec_bridge_retract(spec_t *s);
void spec_none(spec_t *s);

/* ui.c */
void win_open(int x1, int y1, int x2, int y2);
void win_center(int w, int h);
void win_grow(int w, int h);
void win_clear(int x1, int y1, int x2, int y2, int ch);
void print(const char *s);
void print_hi(const char *s);
void print_centered(const char *s);
int wait_key(void);
int input_line(char *buf, int max);
void flush_input(void);
void fade_in(void);
void fade_out(void);
void set_palette_black(void);
void status_screen(void);
void ui_filter_ctrl(int p, ctrl_t *c);              /* co-op: local overlays, votes, queued events */
void ui_emit(int p, int events);                    /* queue one-frame events for player p's next input */
int ui_poll_coop(void);                             /* co-op hotkeys; returns F1/F3 on the map, else 0 */
void ui_run_screen(int scan);                       /* the blocking help or key-setup screen */
void ship_paint(int ctrl_type);                     /* the ship parts window */
void ui_open_ship(int p);                           /* the ship parts overlay, for the player who pressed */
void ui_overlay_hook(void);                         /* draws banners and overlays over the playfield */
void help_screen(void);
void quit_menu(void);
void statue_message(void);
void show_pic(const char *name);
void text_viewer(const uint8_t *text, int toprow, int botrow);
void wait_vbl(int n);
extern int cur_x, cur_y, margin_x;
extern int key_peek(void);
extern int key_get(void);

/* menus.c */
void title_loop(void);
void game_setup(void);              /* start of a game: map position, scores */
void game_new(void);                /* a new game's record: lives, ammo, no ship parts */
void play_game(void);
void save_game_prompt(void);
void online_save_prompt(void);          /* F5 on the world map of an online game: save on the server */
void strip(int row, const char *text);  /* one line on a bar across the top of the screen */
void game_load_record(const void *rec); /* the game starts from this 92 byte record, as if loaded */
int load_game_prompt(void);
void ending(void);
void high_scores_check(void);
void load_scores(void);
void controls_load(void);

#endif
