/* EGA graphics decoding and the frame renderer. */
#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define NUMSPRITES 119
#define NUMBITMAPS 18

typedef struct {
    int w, h;            /* pixels */
    int16_t hb[4];       /* hitbox x1,y1,x2,y2 in 1/256 pixel units */
    uint8_t *pix;        /* w*h colour */
    uint8_t *mask;       /* w*h, 1 = transparent */
} sprite_t;

typedef struct {
    int w, h;
    uint8_t *pix;
} bitmap_t;

extern sprite_t sprites[NUMSPRITES];
extern bitmap_t bitmaps[NUMBITMAPS];

int gfx_load(void);

/* World scroll position (1/256 pixel units), like the game's g_6d00/g_6d04. */
extern int32_t scroll_x, scroll_y;
/* Tile animation speed: frame = (ticks >> anim_shift) & 6. */
extern int anim_shift;
/* Optional hook drawn on top of each refresh (menus, title bitmaps). */
extern void (*rf_hook)(void);

void rf_begin(void);                                /* start a new frame */
int rf_sprite(int32_t x, int32_t y, int sprite);    /* queue a sprite */
int rf_sprite_ex(int32_t x, int32_t y, int sprite, int remap);
void rf_label(int32_t x, int32_t y, const char *text);
int rf_tile(int32_t x, int32_t y, int tile);        /* queue an overlay tile */
void rf_refresh(void);                              /* draw and show the frame */

/* Direct drawing in the game's text coordinates: x in 8-pixel columns of the
 * 48-column page (visible area starts at column 4), y in pixel lines. */
void draw_char(int col, int y, int ch);
void draw_bitmap(int col, int y, int n);
void draw_tile_at(int col, int y, int tile);
void draw_sprite_at(int col, int y, int sprite);
void scroll_text_area(int toprow, int botrow, int up);

#endif
