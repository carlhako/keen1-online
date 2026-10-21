/* EGA graphics: decodes EGAHEAD/EGALATCH/EGASPRIT and draws frames the way
 * the original refresh code composes them (tiles, sprites, overlay tiles). */
#include "gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kdata.h"
#include "platform.h"

sprite_t sprites[NUMSPRITES];
bitmap_t bitmaps[NUMBITMAPS];
static uint8_t font[256][64];
static uint8_t tilepix[NUMTILES][256];

int32_t scroll_x, scroll_y;
int anim_shift = 3;
void (*rf_hook)(void);

/* Draw lists, filled between rf_begin() and rf_refresh(). */
#define MAXRF 512
static struct { int px, py, n, remap; } rf_sprites[MAXRF], rf_tiles[MAXRF];
static int n_rf_sprites, n_rf_tiles;
#define MAXLABELS 16
static struct { int px, py; char text[4]; } rf_labels[MAXLABELS];
static int n_rf_labels;

/* Second colour scheme for player 2's Keen (EGA palette registers): helmet
 * yellow -> green, sweater magenta -> cyan, trousers blue -> red. */
static uint8_t remap_p2[16];
static void remap_init(void)
{
    for (int i = 0; i < 16; i++)
        remap_p2[i] = (uint8_t)i;
    remap_p2[0xe] = 0xa;
    remap_p2[0xd] = 0xb;
    remap_p2[0x9] = 0xc;
}

static void planar(const uint8_t *planes, long planesize, long off, int wbytes, int h,
                   uint8_t *pix, uint8_t *mask)
{
    for (int y = 0; y < h; y++)
        for (int xb = 0; xb < wbytes; xb++) {
            long o = off + (long)y * wbytes + xb;
            uint8_t b0 = planes[o], b1 = planes[planesize + o];
            uint8_t b2 = planes[2 * planesize + o], b3 = planes[3 * planesize + o];
            uint8_t bm = mask ? planes[4 * planesize + o] : 0;
            for (int bit = 0; bit < 8; bit++) {
                int m = 0x80 >> bit;
                int c = ((b0 & m) ? 1 : 0) | ((b1 & m) ? 2 : 0) | ((b2 & m) ? 4 : 0) |
                        ((b3 & m) ? 8 : 0);
                int i = y * wbytes * 8 + xb * 8 + bit;
                pix[i] = (uint8_t)c;
                if (mask)
                    mask[i] = (bm & m) ? 1 : 0;
            }
        }
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

int gfx_load(void)
{
    long hlen, llen, slen;
    uint8_t *head = load_file("EGAHEAD.CK1", &hlen);
    uint8_t *latch = load_file("EGALATCH.CK1", &llen);
    uint8_t *sprit = load_file("EGASPRIT.CK1", &slen);
    if (!head || !latch || !sprit)
        return 0;
    long latchplane = rd32(head + 0), spriteplane = rd32(head + 4);
    long bmptable = rd32(head + 8), sprtable = rd32(head + 12);
    int n8 = rd16(head + 16);
    long off8 = rd32(head + 18);
    int n16 = rd16(head + 28);
    long off16 = rd32(head + 30);
    int nbmp = rd16(head + 34);
    long offbmp = rd32(head + 36);
    int nspr = rd16(head + 40);
    int comp = rd16(head + 46);
    if (comp & 2) {
        long n;
        uint8_t *u = lzw_expand(latch, llen, &n);
        free(latch);
        latch = u;
    }
    if (comp & 1) {
        long n;
        uint8_t *u = lzw_expand(sprit, slen, &n);
        free(sprit);
        sprit = u;
    }
    for (int i = 0; i < n8 && i < 256; i++)
        planar(latch, latchplane, off8 + i * 8, 1, 8, font[i], NULL);
    for (int i = 0; i < n16 && i < NUMTILES; i++)
        planar(latch, latchplane, off16 + i * 32, 2, 16, tilepix[i], NULL);
    for (int i = 0; i < nbmp && i < NUMBITMAPS; i++) {
        const uint8_t *e = head + bmptable + i * 16;
        bitmap_t *b = &bitmaps[i];
        b->w = rd16(e) * 8;
        b->h = rd16(e + 2);
        b->pix = malloc(b->w * b->h);
        planar(latch, latchplane, offbmp + rd32(e + 4), b->w / 8, b->h, b->pix, NULL);
    }
    for (int i = 0; i < nspr && i < NUMSPRITES; i++) {
        const uint8_t *e = head + sprtable + i * 128;
        sprite_t *s = &sprites[i];
        s->w = rd16(e) * 8;
        s->h = rd16(e + 2);
        long off = rd16(e + 6) * 16L + rd16(e + 4);
        for (int k = 0; k < 4; k++)
            s->hb[k] = (int16_t)rd16(e + 8 + k * 2);
        s->pix = malloc(s->w * s->h);
        s->mask = malloc(s->w * s->h);
        planar(sprit, spriteplane, off, s->w / 8, s->h, s->pix, s->mask);
    }
    free(head);
    free(latch);
    free(sprit);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Pixel primitives (screen coordinates, clipped). */

static void put_tile(int px, int py, int tile)
{
    if (tile < 0 || tile >= NUMTILES)
        return;
    const uint8_t *src = tilepix[tile];
    for (int y = 0; y < 16; y++) {
        int sy = py + y;
        if (sy < 0 || sy >= SCR_H)
            continue;
        for (int x = 0; x < 16; x++) {
            int sx = px + x;
            if (sx >= 0 && sx < SCR_W)
                screen[sy * SCR_W + sx] = src[y * 16 + x];
        }
    }
}

static void put_sprite(int px, int py, int n, int remap)
{
    if (n < 0 || n >= NUMSPRITES)
        return;
    const sprite_t *s = &sprites[n];
    if (!remap_p2[0xe])
        remap_init();
    for (int y = 0; y < s->h; y++) {
        int sy = py + y;
        if (sy < 0 || sy >= SCR_H)
            continue;
        for (int x = 0; x < s->w; x++) {
            int sx = px + x;
            int i = y * s->w + x;
            if (sx >= 0 && sx < SCR_W && !s->mask[i])
                screen[sy * SCR_W + sx] = remap ? remap_p2[s->pix[i]] : s->pix[i];
        }
    }
}

static int fine_x(void) { return (scroll_x >> 8) & 15; }
static int fine_y(void) { return (scroll_y >> 8) & 15; }

void draw_char(int col, int y, int ch)
{
    const uint8_t *src = font[ch & 0xff];
    int px = col * 8 - 32 - fine_x();
    for (int yy = 0; yy < 8; yy++) {
        int sy = y + yy;
        if (sy < 0 || sy >= SCR_H)
            continue;
        for (int x = 0; x < 8; x++) {
            int sx = px + x;
            if (sx >= 0 && sx < SCR_W)
                screen[sy * SCR_W + sx] = src[yy * 8 + x];
        }
    }
}

void draw_bitmap(int col, int y, int n)
{
    if (n < 0 || n >= NUMBITMAPS)
        return;
    const bitmap_t *b = &bitmaps[n];
    int px = col * 8 - 32 - fine_x();
    for (int yy = 0; yy < b->h; yy++) {
        int sy = y + yy;
        if (sy < 0 || sy >= SCR_H)
            continue;
        for (int x = 0; x < b->w; x++) {
            int sx = px + x;
            if (sx >= 0 && sx < SCR_W)
                screen[sy * SCR_W + sx] = b->pix[yy * b->w + x];
        }
    }
}

void draw_tile_at(int col, int y, int tile)
{
    put_tile(col * 8 - 32 - fine_x(), y, tile);
}

/* n is sprite*4 + shift, shift being a 2 pixel offset. */
void draw_sprite_at(int col, int y, int n)
{
    put_sprite(col * 8 - 32 - fine_x() + (n & 3) * 2, y, n >> 2, 0);
}

void scroll_text_area(int toprow, int botrow, int up)
{
    int lines = (botrow - toprow) * 8;
    if (lines <= 0)
        return;
    if (up)
        memmove(screen + toprow * 8 * SCR_W, screen + (toprow + 1) * 8 * SCR_W, lines * SCR_W);
    else
        memmove(screen + (toprow + 1) * 8 * SCR_W, screen + toprow * 8 * SCR_W, lines * SCR_W);
}

/* ------------------------------------------------------------------ */
/* Frame lists (the game's 0x5E7C sprite list and 0x6A92 tile list). */

extern int map_w, map_h;

void rf_begin(void)
{
    n_rf_sprites = 0;
    n_rf_tiles = 0;
    n_rf_labels = 0;
}

static int map_tile(int tx, int ty)
{
    if (!map_tiles || tx < 0 || ty < 0 || tx >= map_w || ty >= map_h)
        return 0;
    return MAPT(tx, ty);
}

int rf_sprite(int32_t x, int32_t y, int sprite)
{
    return rf_sprite_ex(x, y, sprite, 0);
}

/* A short text label (opaque font glyphs) at a world position. */
void rf_label(int32_t x, int32_t y, const char *text)
{
    int sx = (int)(x / 256) - (int)((scroll_x / 256) & ~15);
    int sy = (int)(y / 256) - (int)((scroll_y / 256) & ~15);
    if (sx < -32 || sy < -32 || sx > 0x150 || sy > 199 || n_rf_labels >= MAXLABELS)
        return;
    rf_labels[n_rf_labels].px = sx;
    rf_labels[n_rf_labels].py = sy;
    strncpy(rf_labels[n_rf_labels].text, text, 3);
    rf_labels[n_rf_labels].text[3] = 0;
    n_rf_labels++;
}

/* remap: 0 as drawn, 1 player 2's colour scheme. */
int rf_sprite_ex(int32_t x, int32_t y, int sprite, int remap)
{
    int sx = (int)(x / 256) - (int)((scroll_x / 256) & ~15);
    int sy = (int)(y / 256) - (int)((scroll_y / 256) & ~15);
    if (sx < -32 || sy < -32 || sx > 0x150 || sy > 199)
        return 0;
    if (sprite < 0 || sprite >= NUMSPRITES)
        return 0;
    /* Foreground tiles covered by the sprite get redrawn over it. */
    int c0 = ((sx + 32) / 8 - 4) / 2;
    if (c0 < 0)
        c0 = 0;
    else if (c0 > 0x15)
        return 0;
    /* Shifted sprite copies are one byte wider. */
    int wbytes = sprites[sprite].w / 8 + ((sx & 7) / 2 ? 1 : 0);
    int c1 = ((sx + 32) / 8 - 4 + wbytes - 1) / 2;
    if (c1 >= 0x16)
        c1 = 0x15;
    else if (c1 < 0)
        return 0;
    int r0 = sy / 16;
    if (r0 < 0)
        r0 = 0;
    else if (r0 > 0xe)
        return 0;
    int r1 = (sy + sprites[sprite].h - 1) / 16;
    if (r1 >= 0xe)
        r1 = 0xd;
    else if (r1 < 0)
        return 0;
    int stx = (int)(scroll_x >> 12), sty = (int)(scroll_y >> 12);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            int t = map_tile(stx + c, sty + r);
            if (T_BEHAV(t) < 0 && n_rf_tiles < MAXRF) {
                rf_tiles[n_rf_tiles].px = c * 16;
                rf_tiles[n_rf_tiles].py = r * 16;
                rf_tiles[n_rf_tiles].n = t;
                n_rf_tiles++;
            }
        }
    if (n_rf_sprites < MAXRF) {
        rf_sprites[n_rf_sprites].px = sx & ~1;
        rf_sprites[n_rf_sprites].py = sy;
        rf_sprites[n_rf_sprites].n = sprite;
        rf_sprites[n_rf_sprites].remap = remap;
        n_rf_sprites++;
    }
    return 1;
}

int rf_tile(int32_t x, int32_t y, int tile)
{
    int sx = (int)(x / 256) - (int)((scroll_x / 256) & ~15);
    int sy = (int)(y / 256) - (int)((scroll_y / 256) & ~15);
    if (sx < -32 || sy < -32)
        return 0;
    if (n_rf_tiles < MAXRF) {
        rf_tiles[n_rf_tiles].px = (sx + 32) / 8 * 8 - 32;
        rf_tiles[n_rf_tiles].py = sy;
        rf_tiles[n_rf_tiles].n = tile;
        n_rf_tiles++;
    }
    return 1;
}

void rf_refresh(void)
{
    if (plat_fast)
        return;
    int fx = fine_x(), fy = fine_y();
    int stx = (int)(scroll_x >> 12), sty = (int)(scroll_y >> 12);
    /* drawing only: tile animation does not touch the game state */
    int frame = ((plat_ticks() >> anim_shift) & 6) / 2;
    for (int ty = 0; ty < 14; ty++)
        for (int tx = 0; tx < 21; tx++) {
            int t = map_tile(stx + tx, sty + ty);
            put_tile(tx * 16 - fx, ty * 16 - fy, tile_gfx(t, frame));
        }
    for (int i = 0; i < n_rf_sprites; i++)
        put_sprite(rf_sprites[i].px - fx, rf_sprites[i].py - fy, rf_sprites[i].n,
                   rf_sprites[i].remap);
    for (int i = 0; i < n_rf_tiles; i++)
        put_tile(rf_tiles[i].px - fx, rf_tiles[i].py - fy, tile_gfx(rf_tiles[i].n, frame));
    for (int i = 0; i < n_rf_labels; i++)
        for (int c = 0; rf_labels[i].text[c]; c++) {
            const uint8_t *g = font[(uint8_t)rf_labels[i].text[c]];
            int px = rf_labels[i].px - fx + c * 8, py = rf_labels[i].py - fy;
            for (int yy = 0; yy < 8; yy++)
                for (int xx = 0; xx < 8; xx++) {
                    int sx = px + xx, sy = py + yy;
                    if (sx >= 0 && sx < SCR_W && sy >= 0 && sy < SCR_H)
                        screen[sy * SCR_W + sx] = g[yy * 8 + xx];
                }
        }
    if (rf_hook)
        rf_hook();
    plat_vsync(1);
}
