/*
 * md_s24.h — the Mega Drive picture on the Model 2's System 24 tilemaps (MAME
 * sega/segaic24.cpp, as sega/model2_v.cpp composes it), from the VDP state in md_hw.h.
 *
 * The Model 2 has two scrolling tilemap planes, each 64x64 cells of 8x8 4bpp chars with
 * per-line horizontal scroll: exactly a Mega Drive plane. So
 *   Mega Drive plane B -> tilemap layer 2 (drawn opaque: its pen 0 is the backdrop colour)
 *   Mega Drive plane A -> tilemap layer 0
 *   Mega Drive sprites -> GEO polygons, drawn between the tilemaps' low and high passes
 * and the hardware does the scrolling. The Mega Drive's 320x224 screen sits at (88,80) in
 * the Model 2's 496x384; the window layers 1 and 3 (selected per 8 pixels by the mask)
 * cover the rest: layer 3 black, layer 1 free for text (s24_text).
 *
 * Chars. A Mega Drive name entry picks a pattern, a flip and a palette line; a System 24
 * entry picks a char, and the char's number fixes its palette bank (bank = char >> 7). So
 * each (pattern, flip, line) the planes use gets its own char, in a 128-char group whose
 * bank holds that line's colours, allocated on first use and redrawn when the pattern
 * changes. A Mega Drive pattern row is already in char-RAM order (the 16-bit words are
 * the VRAM's), so an unflipped char is a straight copy.
 *
 * Sprites. Each sprite is one textured polygon (m2_sprite.h palette mode: the texels are
 * the sprite's pens, a colour table row holds its palette line), so the i960 only lists
 * them; the GEO draws the pixels. Sprite images are drawn into a texture atlas on first use
 * and again when a pattern of them changes. Priority: no tile entry keeps its priority bit;
 * the polygons are layered low sprites, then plane B's and plane A's high-priority cells
 * where they cover a low sprite, then high sprites (s24_sprites). Line limits (masking, the
 * 320 pixels a line) cut the polygons.
 *
 * Not done: the window plane, shadow/highlight, H32, 2-cell vertical scroll, per-line
 * palette changes (Labyrinth Zone's water line). Plane A's low-priority pixels over plane B's
 * high-priority ones show plane A (Sonic 1 has none).
 *
 * Portable: the includer defines S24_TILE / S24_CHAR / S24_PAL as u16 pointers to tile RAM
 * (0x8000 words), char RAM (0x40000 words) and palette RAM (0x1000 words): the hardware
 * on the i960 (src/sonic.c), arrays on the host (tools/mds24.c renders them to check
 * this against src/md_render.h), and the S24_GPU_* hooks for the polygons (sonic.c:
 * m2_sprite.h). Include after md_hw.h and m2font.h (gFont).
 */
#ifndef MD_S24_H
#define MD_S24_H

#define S24_X0 88                        /* Mega Drive screen origin on the Model 2 screen */
#define S24_Y0 80
#define S24_LAYER_A   0x0000u            /* tile RAM word offsets of the four layers */
#define S24_LAYER_AW  0x1000u
#define S24_LAYER_B   0x2000u
#define S24_LAYER_BW  0x3000u

#define S24_VAR_GROUPS 120               /* groups 0-119: (pattern, flip, line) chars */
#define S24_UI_GROUP   120               /* the font (m2font gFont) */
#define S24_BLK_GROUP  121               /* its char 0: solid pen 1 (black) */

/* Mega Drive colour -> BGR555 (3 bits -> 5) */
static u16 s24_rgb(u16 cram) {
    static const u8 c5[8] = { 0, 4, 9, 13, 18, 22, 27, 31 };
    return (u16)(c5[(cram >> 1) & 7] | (c5[(cram >> 5) & 7] << 5) | (c5[(cram >> 9) & 7] << 10));
}

/* ---- state --------------------------------------------------------------------------- */
static u16 s24_var[2048][16];            /* char of (pattern, flip * 4 + line) */
static u16 s24_varmask[2048];            /* which of the 16 exist */
static u8  s24_grp_line[128];            /* palette line of a variant group */
static u8  s24_grp_used[128];            /* chars allocated in it */
static u8  s24_line_grp[4];              /* the group each line allocates from, 0xff = none */
static u8  s24_ngroups;                  /* variant groups taken */
static u8  s24_full;                     /* out of chars: start over next frame */
static u16 s24_ntA[4096], s24_ntB[4096]; /* the name entries last written, 0xffff = redo */
static u8  s24_pw, s24_ph;               /* plane size in cells (32 or 64) as last mapped */
static u32 s24_ntA_at = 0xffffffffu, s24_ntB_at;  /* ... and the name tables' VRAM words */
static u8  s24_nt_all = 1;               /* redo every entry (not only the written ones) */
static u16 s24_cram[64];                 /* colours last written */
static u8  s24_bg = 0xff;                /* backdrop index last written */
static u8  s24_nib_rev[256];             /* a byte with its two pixels swapped (h-flip) */
static u16 s24_hsA[224], s24_hsB[224];   /* this frame's horizontal scroll per line */
static u16 s24_hsA_w[224], s24_hsB_w[224];   /* ... as last written */
static u8  s24_blank = 0xff;             /* display disabled, as last written */
static u16 s24_vsA, s24_vsB;             /* this frame's vertical scroll, written at the swap */
static u8  s24_blank_n;                  /* this frame's display disable, written at the swap */

/* sprites: GEO polygons (s24_sprites) */
#define S24_MAXSPR 81                    /* the sprite list as far as the VDP reads it */
#define S24_SLOTS  1024                  /* texture slots of 32x32 texels: a 1024x1024 atlas */
static u8  s24_stop_o[224];              /* per line: the sprite (list order) drawing stops at, */
static u16 s24_stop_p[224];              /* ... and its first pixel not drawn (MAME's rules) */
static u8  s24_cut;                      /* some line is cut this frame */
static u32 s24_slot_key[S24_SLOTS];      /* pattern | (cw - 1) << 11 | (ch - 1) << 13 | 1 << 16 */
static u32 s24_slot_made[S24_SLOTS];     /* the picture (s24_gen) it was drawn for */
static u32 s24_slot_used[S24_SLOTS];     /* the last picture that showed it */
static u16 s24_tex_of[4096];             /* key hash -> slot + 1 */
static u32 s24_tile_gen[2048];           /* the picture whose build saw the pattern written */
static u32 s24_gen = 3, s24_slot_next;   /* this picture; where the slot search goes on */
static u16 s24_spr_cram[64];             /* sprite colours last written */
static u32 s24_nquads, s24_nuploads;     /* statistics: polygons, cells drawn into the atlas */

/* a row of eight pixels mirrored (shifts: the i960 takes 4 cycles a load) */
static inline u32 s24_rev32(u32 r) {
    r = ((r >> 4) & 0x0f0f0f0fu) | ((r & 0x0f0f0f0fu) << 4);
    r = ((r >> 8) & 0x00ff00ffu) | ((r & 0x00ff00ffu) << 8);
    return (r << 16) | (r >> 16);
}
/* a pattern row (two VRAM words, the first the left half) in one load: little-endian
 * hosts and the i960 alike */
typedef u32 s24_u32a __attribute__((may_alias));
static inline u32 s24_row(const u16 *p) {
    u32 v = *(const s24_u32a *)p;
    return (v << 16) | (v >> 16);
}
/* the index of the lowest set bit of x (not 0) */
static inline u32 s24_low_bit(u32 x) {
#ifdef __i960__
    u32 r;
    x &= -x;
    __asm__("scanbit %1,%0" : "=r"(r) : "r"(x));
    return r;
#else
    return (u32)__builtin_ctz(x);
#endif
}
/* a row into char RAM in one store (its two 16-bit words; the bus splits it) */
static inline void s24_char_row(volatile u16 *d, u32 w) {
    *(volatile s24_u32a *)d = (w << 16) | (w >> 16);
}
/* 0xf in each nibble that is not 0 */
static inline u32 s24_opaque(u32 r) {
    r |= r >> 1; r |= r >> 2;
    return (r & 0x11111111u) * 15u;
}

/* ---- chars ----------------------------------------------------------------------------- */
static void s24_draw_char(u32 tile, u32 flip, u32 idx) {
    const u16 *src = &md_vram[(tile & 0x7ff) * 16];
    volatile u16 *dst = &S24_CHAR[idx * 16];
    int r;
    for (r = 0; r < 8; r++) {
        const u16 *s = src + ((flip & 2) ? 7 - r : r) * 2;
        if (flip & 1) {
            u16 w0 = s[0], w1 = s[1];
            dst[r * 2]     = (u16)((s24_nib_rev[w1 & 0xff] << 8) | s24_nib_rev[w1 >> 8]);
            dst[r * 2 + 1] = (u16)((s24_nib_rev[w0 & 0xff] << 8) | s24_nib_rev[w0 >> 8]);
        } else {
            dst[r * 2] = s[0]; dst[r * 2 + 1] = s[1];
        }
    }
}

static void s24_bank_colours(u32 g, u32 line) {
    int i;
    S24_PAL[g * 16] = s24_rgb(md_cram[md_reg[7] & 0x3f]);
    for (i = 1; i < 16; i++) S24_PAL[g * 16 + i] = s24_rgb(md_cram[line * 16 + i]);
}

/* the char for a pattern/flip/line, drawn and allocated on first use (0 when out of room) */
static u16 s24_char_of(u32 tile, u32 flip, u32 line) {
    u32 v = flip * 4 + line, g;
    if (s24_varmask[tile] & (1u << v)) return s24_var[tile][v];
    g = s24_line_grp[line];
    if (g == 0xff || s24_grp_used[g] >= 128) {
        if (s24_ngroups >= S24_VAR_GROUPS) { s24_full = 1; return 0; }
        g = s24_ngroups++;
        s24_line_grp[line] = (u8)g;
        s24_grp_line[g] = (u8)line;
        s24_grp_used[g] = g == 0 ? 1 : 0;           /* char 0 stays blank */
        s24_bank_colours(g, line);
    }
    {
        u16 idx = (u16)(g * 128 + s24_grp_used[g]++);
        s24_var[tile][v] = idx;
        s24_varmask[tile] |= (u16)(1u << v);
        s24_draw_char(tile, flip, idx);
        return idx;
    }
}

static inline u16 s24_entry(u16 e) {
    return (u16)((e & 0x8000) | s24_char_of(e & 0x7ff, (e >> 11) & 3, (e >> 13) & 3));
}

/* forget every char (out of room, e.g. after many level loads): all is redrawn */
static void s24_flush(void) {
    int i;
    for (i = 0; i < 2048; i++) s24_varmask[i] = 0;
    for (i = 0; i < 4096; i++) { s24_ntA[i] = 0xffff; s24_ntB[i] = 0xffff; }
    s24_nt_all = 1;
    for (i = 0; i < 4; i++) s24_line_grp[i] = 0xff;
    for (i = 0; i < S24_VAR_GROUPS; i++) s24_grp_line[i] = 0xff;
    s24_ngroups = 0; s24_full = 0;
}

/* text on layer 1 (the window around the picture): 8x8 cells, col 0-61, row 0-47 */
static void s24_text(int col, int row, const char *s) {
    for (; *s && col < 62; s++, col++)
        S24_TILE[S24_LAYER_AW + row * 64 + col] = (u16)(0x8000 | (S24_UI_GROUP * 128 + (*s & 0x7f)));
}

static void s24_init(void) {
    int i, y;
    for (i = 0; i < 256; i++) s24_nib_rev[i] = (u8)((i >> 4) | ((i & 15) << 4));
    s24_flush();
    for (i = 0; i < 64; i++) s24_cram[i] = 0xffff;
    for (i = 0; i < 224; i++) { s24_hsA_w[i] = 0xffff; s24_hsB_w[i] = 0xffff; }
    for (i = 0; i < 64; i++) s24_spr_cram[i] = 0xffff;
    for (i = S24_VAR_GROUPS; i < 128; i++) s24_grp_line[i] = 0xff;
    /* the font (group 120, bank 120: pen 1 white, 2 grey) and the black char (group 121) */
    for (i = 0; i < (int)sizeof(gFont) / 2; i++)
        S24_CHAR[S24_UI_GROUP * 128 * 16 + i] = (u16)(gFont[2 * i] | (gFont[2 * i + 1] << 8));
    for (i = 0; i < 16; i++) S24_CHAR[S24_BLK_GROUP * 128 * 16 + i] = 0x1111;
    for (i = 0; i < 16; i++) { S24_PAL[S24_UI_GROUP * 16 + i] = 0; S24_PAL[S24_BLK_GROUP * 16 + i] = 0; }
    S24_PAL[S24_UI_GROUP * 16 + 1] = 0x7fff;
    S24_PAL[S24_UI_GROUP * 16 + 2] = 0x2108;
    /* layers: A and B blank, the window layers transparent (1) and black (3) */
    for (i = 0; i < 4096; i++) {
        S24_TILE[S24_LAYER_A + i] = 0;
        S24_TILE[S24_LAYER_B + i] = 0;
        S24_TILE[S24_LAYER_AW + i] = (u16)(S24_UI_GROUP * 128 + ' ');
        S24_TILE[S24_LAYER_BW + i] = (u16)(S24_BLK_GROUP * 128);
    }
    /* the masks: the window layers everywhere but the 320x224 picture (cells 11-50) */
    for (y = 0; y < 384; y++) {
        int in = y >= S24_Y0 && y < S24_Y0 + 224;
        u16 m0 = in ? 0xffe0 : 0xffff, m1 = in ? 0 : 0xffff, m3 = in ? 0x1fff : 0xffff;
        S24_TILE[0x6000 + y * 4 + 0] = m0; S24_TILE[0x6000 + y * 4 + 1] = m1;
        S24_TILE[0x6000 + y * 4 + 2] = m1; S24_TILE[0x6000 + y * 4 + 3] = m3;
        S24_TILE[0x6800 + y * 4 + 0] = m0; S24_TILE[0x6800 + y * 4 + 1] = m1;
        S24_TILE[0x6800 + y * 4 + 2] = m1; S24_TILE[0x6800 + y * 4 + 3] = m3;
    }
    /* scroll: layers 0 and 2 per-line (bit 15); windows fixed; all enabled */
    S24_TILE[0x5000] = 0x8000; S24_TILE[0x5001] = 0; S24_TILE[0x5002] = 0x8000; S24_TILE[0x5003] = 0;
    S24_TILE[0x5004] = 0; S24_TILE[0x5005] = 0; S24_TILE[0x5006] = 0; S24_TILE[0x5007] = 0;
    for (i = 0; i < 0x200; i++) { S24_TILE[0x4000 + i] = 0; S24_TILE[0x4400 + i] = 0; }
}

/* ---- sprites ---------------------------------------------------------------------------- */
/* where each line's sprites end, as MAME's render_spriteline_to_spritebuffer: masking (a
 * sprite at x = 0 and one at 0 < x < 0x40 on the line, either order: that sprite and the
 * rest go), the 320-pixel budget a line (transparent pixels count), 81 sprites down the list.
 * s24_stop_o/p: sprite order and pixel; 0xff = the line is not cut. */
static void s24_sprite_limits(void) {
    u32 base = (u32)(md_reg[5] & 0x7e) << 9, link = 0, o = 0;
    static u8 mask[224];
    static int budget[224];
    int left = 80, i;
    for (i = 0; i < 224; i++) { mask[i] = 0; budget[i] = 320; s24_stop_o[i] = 0xff; }
    s24_cut = 0;
    do {
        u32 a = (base + link * 8) & 0xffff;
        u16 w0 = md_vram[a >> 1], w1 = md_vram[(a >> 1) + 1], w3 = md_vram[(a >> 1) + 3];
        int sy = (int)(w0 & 0x1ff) - 128, xpos = (int)(w3 & 0x1ff), w = ((((w1 >> 10) & 3) + 1) * 8);
        int y0 = sy < 0 ? 0 : sy, y1 = sy + (((w1 >> 8) & 3) + 1) * 8, y;
        link = w1 & 0x7f;
        if (y1 > 224) y1 = 224;
        for (y = y0; y < y1; y++) {
            if (s24_stop_o[y] != 0xff) continue;
            if (xpos == 0) mask[y] |= 1;
            if (xpos > 0 && xpos < 0x40) mask[y] |= 2;
            if (mask[y] == 3) { s24_stop_o[y] = (u8)o; s24_stop_p[y] = 0; s24_cut = 1; continue; }
            if (budget[y] <= w) { s24_stop_o[y] = (u8)o; s24_stop_p[y] = (u16)budget[y]; s24_cut = 1; continue; }
            budget[y] -= w;
        }
        o++; left--;
    } while (left >= 0 && link != 0);
}

/* the pixels of sprite o on line y it may draw, from its left edge: w = all of them */
static inline int s24_allowed(u32 o, int y, int w) {
    u32 so = s24_stop_o[y];
    if (so == 0xff || o < so) return w;
    return o > so ? 0 : (int)s24_stop_p[y];
}

static int s24_popc(u32 v) { int n = 0; while (v) { v &= v - 1; n++; } return n; }
static void s24_nt(u32 layer, u16 *shadow, u32 base, u32 i);

/* a pattern of nothing but pen 0 */
static int s24_empty(u32 t) {
    const s24_u32a *p = (const s24_u32a *)&md_vram[(t & 0x7ff) * 16];
    return !(p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7]);
}

/* The texture slot of a sprite image (cw x ch cells from pattern t, column by column, not
 * flipped), drawn into the atlas on first use and again after a pattern of it changed. A new
 * image never goes into a slot one of the last pictures showed: the GEO reads texture RAM when
 * it draws the shown picture's polygons, at the end of each vblank. -1 when all are taken. */
static int s24_tex(u32 t, u32 cw, u32 ch) {
    u32 key = t | (cw - 1) << 11 | (ch - 1) << 13 | 0x10000u, h = (t ^ key >> 4) & 4095, n = cw * ch, i, s;
    if ((s = s24_tex_of[h]) != 0 && s24_slot_key[--s] == key) {
        u32 made = s24_slot_made[s];
        for (i = 0; i < n; i++) if (s24_tile_gen[(t + i) & 0x7ff] > made) break;
        if (i == n) { s24_slot_used[s] = s24_gen; return (int)s; }
    }
    for (i = 0; i < S24_SLOTS; i++) {
        s = s24_slot_next;
        s24_slot_next = (s24_slot_next + 1) & (S24_SLOTS - 1);
        if (s24_slot_used[s] + 2u < s24_gen) break;
    }
    if (i == S24_SLOTS) return -1;
    s24_slot_key[s] = key; s24_slot_made[s] = s24_gen; s24_slot_used[s] = s24_gen;
    s24_tex_of[h] = (u16)(s + 1);
    {
        u32 c, r, k, rows[8];
        for (c = 0; c < cw; c++)
            for (r = 0; r < ch; r++) {
                const u16 *pat = &md_vram[((t + c * ch + r) & 0x7ff) * 16];
                for (k = 0; k < 8; k++) rows[k] = s24_row(pat + k * 2);
                S24_GPU_CELL((s & 31) * 32 + c * 8, (s >> 5) * 32 + r * 8, rows);
            }
    }
    s24_nuploads += n;
    return (int)s;
}

/* the part [ox0,ox1) x [oy0,oy1) of the w x h image in slot s, as shown (flipped), with the
 * image's top-left at screen (x,y): one polygon */
static void s24_quad(int s, int x, int y, int w, int h, int ox0, int oy0, int ox1, int oy1,
                     u32 flip, u32 line, int layer) {
    u32 tu, tv;
    if (x + ox0 < 0) ox0 = -x;
    if (y + oy0 < 0) oy0 = -y;
    if (x + ox1 > 320) ox1 = 320 - x;
    if (y + oy1 > 224) oy1 = 224 - y;
    if (ox0 >= ox1 || oy0 >= oy1) return;
    tu = (u32)(s & 31) * 32u + (u32)((flip & 1) ? w - ox1 : ox0);
    tv = (u32)(s >> 5) * 32u + (u32)((flip & 2) ? h - oy1 : oy0);
    S24_GPU_QUAD(x + ox0, y + oy0, ox1 - ox0, oy1 - oy0, tu, tv, flip, line, layer);
    s24_nquads++;
}

/* A high-priority cell of plane A or B under a high-priority sprite is demoted: its tile
 * entries lose the priority bit (so the polygon sprite, drawn before the tilemaps' high pass,
 * shows over it) and the cell is drawn again as polygons, on a layer between the low and the
 * high sprites (s24_cell_polys). s24_dem lists them per plane; s24_dem_shown those on screen. */
#define S24_MAXDEM 256
static u16 s24_dem[2][S24_MAXDEM], s24_dem_shown[2][S24_MAXDEM];
static u16 s24_ndem[2], s24_ndem_shown[2];
static u8  s24_dem_mark[2][4096];        /* bit 0: in s24_dem, bit 1: in s24_dem_shown */

static void s24_dem_clear(void) {
    u32 p, i;
    for (p = 0; p < 2; p++) {
        for (i = 0; i < s24_ndem[p]; i++) s24_dem_mark[p][s24_dem[p][i]] &= (u8)~1;
        s24_ndem[p] = 0;
    }
}

/* the planes' geometry for the build: name table (VRAM words), size, scroll per line */
typedef struct { u32 nt, pw, ph, vs; const u16 *hs; } s24_plane_t;

/* demote the high-priority, not empty cells of plane p over screen [x0,x1) x [y0,y1) */
static void s24_demote(const s24_plane_t *P, u32 p, int x0, int y0, int x1, int y1) {
    u32 mx = P->pw * 8 - 1, my = P->ph * 8 - 1;
    int y, x;
    for (y = y0; y < y1; y++) {
        u32 py = ((u32)y + P->vs) & my, h = P->hs[y];
        if (y != y0 && (py & 7) && h == P->hs[y - 1]) continue;   /* same cells as the line above */
        for (x = x0 - (int)((((u32)x0 - h) & mx) & 7); x < x1; x += 8) {
            u32 k = (py >> 3) * P->pw + ((((u32)x - h) & mx) >> 3);
            u16 e = md_vram[(P->nt + k) & 0x7fff];
            if (!(e & 0x8000) || (s24_dem_mark[p][k] & 1) || s24_ndem[p] >= S24_MAXDEM || s24_empty(e)) continue;
            s24_dem_mark[p][k] |= 1;
            s24_dem[p][s24_ndem[p]++] = (u16)k;
        }
    }
}

/* cell k of plane p wherever it is on screen, as polygons on `layer`: one per run of lines
 * whose scroll puts it at the same x */
static void s24_cell_polys(const s24_plane_t *P, u32 k, int layer) {
    u32 mx = P->pw * 8 - 1, my = P->ph * 8 - 1, cx = k % P->pw, cy = k / P->pw, r, r0 = 0;
    u16 e = md_vram[(P->nt + k) & 0x7fff];
    int s = s24_tex(e & 0x7ff, 1, 1), x = 0, y = 0, px = 0, py = 0, run = 0;
    if (s < 0) return;
    for (r = 0; r <= 8; r++) {
        if (r < 8) {
            y = (int)((cy * 8 + r - P->vs) & my);
            if (y < 224) {
                x = (int)((cx * 8 + P->hs[y]) & mx);
                if (x > (int)mx - 7) x -= (int)mx + 1;
            }
        }
        if (run && (r == 8 || y >= 224 || y != py + 1 || x != px)) {
            /* lines r0..r-1 of the cell, at x px, from screen line py - (r - 1 - r0) */
            s24_quad(s, px, py - (int)(r - 1), 8, 8, 0, (int)r0, 8, (int)r, (u32)(e >> 11) & 3, (u32)(e >> 13) & 3, layer);
            run = 0;
        }
        if (r < 8 && y < 224 && x < 320) {
            if (!run) { run = 1; r0 = r; }
            px = x; py = y;
        }
    }
}

/* The sprites as GEO polygons, between the tilemaps' low and high passes: a low-priority
 * sprite is then where the Mega Drive has it. A high-priority one is on top only after the
 * high-priority cells it covers are demoted (s24_demote). The polygons' layers, back to front:
 * 0 the low-priority sprites, 1 plane B's demoted cells, 2 plane A's, 3 the high-priority
 * sprites. In a layer the later polygon is in front, so the list goes last sprite first. A
 * low-priority sprite over a later one on layer 3 goes on layer 3 too: between sprites the list
 * order decides. One polygon a sprite, more where a line limit cuts it (s24_sprite_limits).
 * Wrong only where a demoted cell is over such a sprite, and where a demoted cell of plane A
 * reaches past the sprite over a high-priority pixel of plane B. */
static __attribute__((noinline)) void s24_sprites(u32 vsA, u32 vsB) {
    static u16 sw1[S24_MAXSPR], sw2[S24_MAXSPR];
    static short ssx[S24_MAXSPR], ssy[S24_MAXSPR], hb[S24_MAXSPR][4];
    u32 nh = 0, base = (u32)(md_reg[5] & 0x7e) << 9, link = 0, n = 0, i, p;
    u32 pw = (md_reg[16] & 3) == 0 ? 32 : 64, ph = ((md_reg[16] >> 4) & 3) == 0 ? 32 : 64;
    s24_plane_t P[2];
    int left = 80;
    P[0].nt = ((u32)(md_reg[4] & 7) << 13) >> 1; P[0].vs = vsB; P[0].hs = s24_hsB;
    P[1].nt = ((u32)(md_reg[2] & 0x38) << 10) >> 1; P[1].vs = vsA; P[1].hs = s24_hsA;
    P[0].pw = P[1].pw = pw; P[0].ph = P[1].ph = ph;
    s24_dem_clear();
    s24_sprite_limits();
    do {
        u32 a = (base + link * 8) & 0xffff;
        u16 w0 = md_vram[a >> 1], w1 = md_vram[(a >> 1) + 1];
        sw1[n] = w1; sw2[n] = md_vram[(a >> 1) + 2];
        ssy[n] = (short)((int)(w0 & 0x1ff) - 128); ssx[n] = (short)((int)(md_vram[(a >> 1) + 3] & 0x1ff) - 128);
        link = w1 & 0x7f;
        n++; left--;
    } while (left >= 0 && link != 0);
    for (i = n; i-- > 0; ) {
        u32 w1 = sw1[i], w2 = sw2[i], cw = ((w1 >> 10) & 3) + 1, ch = ((w1 >> 8) & 3) + 1;
        u32 flip = (w2 >> 11) & 3, line = (w2 >> 13) & 3;
        int sx = ssx[i], sy = ssy[i], W = (int)cw * 8, H = (int)ch * 8, layer = (w2 & 0x8000) ? 3 : 0, s;
        int x0 = sx < 0 ? 0 : sx, y0 = sy < 0 ? 0 : sy, x1 = sx + W > 320 ? 320 : sx + W, y1 = sy + H > 224 ? 224 : sy + H;
        if (x0 >= x1 || y0 >= y1 || (s = s24_tex(w2 & 0x7ff, cw, ch)) < 0) continue;
        if (!layer)
            for (p = 0; p < nh; p++)
                if (x0 < hb[p][2] && hb[p][0] < x1 && y0 < hb[p][3] && hb[p][1] < y1) { layer = 3; break; }
        if (layer) { hb[nh][0] = (short)x0; hb[nh][1] = (short)y0; hb[nh][2] = (short)x1; hb[nh][3] = (short)y1; nh++; }
        if (!s24_cut) s24_quad(s, sx, sy, W, H, x0 - sx, y0 - sy, x1 - sx, y1 - sy, flip, line, layer);
        else {
            int y = y0, ye, aw, xe;
            for (; y < y1; y = ye) {                   /* runs of lines cut alike */
                aw = s24_allowed(i, y, W);
                for (ye = y + 1; ye < y1 && s24_allowed(i, ye, W) == aw; ye++) { }
                xe = sx + aw > x1 ? x1 : sx + aw;
                if (xe > x0) s24_quad(s, sx, sy, W, H, x0 - sx, y - sy, xe - sx, ye - sy, flip, line, layer);
            }
        }
        if (w2 & 0x8000) { s24_demote(&P[0], 0, x0, y0, x1, y1); s24_demote(&P[1], 1, x0, y0, x1, y1); }
    }
    for (p = 0; p < 2; p++)
        for (i = 0; i < s24_ndem[p]; i++) s24_cell_polys(&P[p], s24_dem[p][i], 1 + (int)p);
}

/* at the swap, after the planes: the cells demoted last picture and not now get their
 * entries back; the ones demoted now lose the priority bit */
static void s24_demote_swap(void) {
    static const u32 layer[2] = { S24_LAYER_B, S24_LAYER_A };
    u32 p, i, x, y, pw = s24_pw, ph = s24_ph;
    for (p = 0; p < 2; p++) {
        u16 *shadow = p ? s24_ntA : s24_ntB;
        u32 at = p ? s24_ntA_at : s24_ntB_at;
        for (i = 0; i < s24_ndem_shown[p]; i++) {
            u32 k = s24_dem_shown[p][i];
            s24_dem_mark[p][k] &= (u8)~2;
            if (!(s24_dem_mark[p][k] & 1) && k < pw * ph) { shadow[k] = 0xffff; s24_nt(layer[p], shadow, at, k); }
        }
        for (i = 0; i < s24_ndem[p]; i++) {
            u32 k = s24_dem[p][i], gx = k & (pw - 1), gy = k >> (pw == 64 ? 6 : 5);
            u16 t;
            if (k >= pw * ph) continue;
            t = (u16)(s24_entry(md_vram[(at + k) & 0x7fff]) & 0x7fff);
            for (y = gy; y < 64; y += ph) for (x = gx; x < 64; x += pw) S24_TILE[layer[p] + y * 64 + x] = t;
            s24_dem_mark[p][k] |= 2;
            s24_dem_shown[p][i] = (u16)k;
        }
        s24_ndem_shown[p] = s24_ndem[p];
    }
}

/* the planes: the chars of patterns that changed, the name entries the game wrote (part of
 * the swap: they are on screen as they are written) */
static __attribute__((noinline)) void s24_planes(void) {
    u32 ntA = ((u32)(md_reg[2] & 0x38) << 10) >> 1, ntB = ((u32)(md_reg[4] & 7) << 13) >> 1, i;
    u32 nd[64];

    if (s24_full) s24_flush();

    /* the name tables' 32-byte chunks the game wrote (VRAM marks, taken before the
     * pattern pass below clears them) */
    {
        u32 w;
        for (w = 0; w < 64; w++) nd[w] = md_tile_any ? md_tile_dirty[w] : 0;
    }

    /* patterns that changed: redraw their chars */
    if (md_tile_any) {
        u32 w;
        for (w = 0; w < 64; w++) {
            u32 bits = md_tile_dirty[w];
            while (bits) {
                u32 b = 0, t, m;
                while (!((bits >> b) & 1)) b++;
                bits &= ~(1u << b);
                t = w * 32 + b;
                for (m = s24_varmask[t]; m; ) {
                    u32 v = 0;
                    while (!((m >> v) & 1)) v++;
                    m &= ~(1u << v);
                    s24_draw_char(t, v >> 2, s24_var[t][v]);
                }
            }
            md_tile_dirty[w] = 0;
        }
        md_tile_any = 0;
    }

    /* name tables: a 32- or 64-cell plane repeated over the 64x64 layer */
    {
        u32 pw = (md_reg[16] & 3) == 0 ? 32 : 64, ph = ((md_reg[16] >> 4) & 3) == 0 ? 32 : 64, n = pw * ph, c;
        if (pw != s24_pw || ph != s24_ph || ntA != s24_ntA_at || ntB != s24_ntB_at) {
            s24_pw = (u8)pw; s24_ph = (u8)ph; s24_ntA_at = ntA; s24_ntB_at = ntB;
            for (i = 0; i < 4096; i++) { s24_ntA[i] = 0xffff; s24_ntB[i] = 0xffff; }
            s24_nt_all = 1;
        }
        for (c = 0; c < n / 16; c++) {                   /* 16 entries a chunk */
            u32 ka = (ntA * 2 >> 5) + c, kb = (ntB * 2 >> 5) + c, k;
            if (s24_nt_all || ((nd[(ka >> 5) & 63] >> (ka & 31)) & 1))
                for (k = c * 16; k < c * 16 + 16; k++) s24_nt(S24_LAYER_A, s24_ntA, ntA, k);
            if (s24_nt_all || ((nd[(kb >> 5) & 63] >> (kb & 31)) & 1))
                for (k = c * 16; k < c * 16 + 16; k++) s24_nt(S24_LAYER_B, s24_ntB, ntB, k);
        }
        s24_nt_all = 0;
    }
}

/* what s24_swap will take, in timer ticks (25 MHz): its work counted, at rates fitted to
 * MAME runs (the name entries dominate: 1.7k a chunk the game wrote, up to 600k at a zone's
 * start) */
static u32 s24_swap_cost(void) {
    u32 ntA = ((u32)(md_reg[2] & 0x38) << 10) >> 1, ntB = ((u32)(md_reg[4] & 7) << 13) >> 1;
    u32 pw = (md_reg[16] & 3) == 0 ? 32 : 64, ph = ((md_reg[16] >> 4) & 3) == 0 ? 32 : 64, n = pw * ph;
    u32 chars = 0, chunks = 0, w, c;
    if (md_tile_any)
        for (w = 0; w < 64; w++) {
            u32 bits = md_tile_dirty[w], b;
            for (b = 0; bits; b++, bits >>= 1) if (bits & 1) chars += (u32)s24_popc(s24_varmask[w * 32 + b]);
        }
    if (s24_full || s24_nt_all || pw != s24_pw || ph != s24_ph || ntA != s24_ntA_at || ntB != s24_ntB_at)
        chunks = n / 8;
    else if (md_tile_any)
        for (c = 0; c < n / 16; c++) {
            u32 ka = (ntA * 2 >> 5) + c, kb = (ntB * 2 >> 5) + c;
            chunks += ((md_tile_dirty[(ka >> 5) & 63] >> (ka & 31)) & 1) + ((md_tile_dirty[(kb >> 5) & 63] >> (kb & 31)) & 1);
        }
    return 11500u + 361u * chars + 1713u * chunks + 300u * (u32)(s24_ndem[0] + s24_ndem[1] + s24_ndem_shown[0] + s24_ndem_shown[1]);
}

/* the swap: what s24_build made goes on screen, all at once. The sprite polygons (the GEO
 * takes the list at the next interrupt) and their colours, the planes' changes, the demoted
 * cells, the colours (Sonic cycles them), the scroll. A screen drawn in the middle mixes two
 * frames: sonic.c starts the swap only when it ends before MAME draws the Model 2's screen,
 * at the end of vblank (s24_swap_cost). */
static __attribute__((noinline)) void s24_swap(void) {
    u32 i, bg = md_reg[7] & 0x3f;
    S24_GPU_COMMIT();                                   /* first: the GEO takes it at vblank */
    for (i = 0; i < 64; i++) {                          /* the sprites' colours, for that list */
        u16 c = md_cram[i];
        if (!(i & 15) || c == s24_spr_cram[i]) continue;
        s24_spr_cram[i] = c;
        S24_GPU_PEN(i >> 4, i & 15, s24_rgb(c));
    }
    s24_planes();
    s24_demote_swap();
    /* colours: the lines' banks, the backdrop (pen 0 of every variant bank) */
    for (i = 0; i < 64; i++) {
        u16 c = md_cram[i];
        if (c == s24_cram[i]) continue;
        s24_cram[i] = c;
        if (i & 15) {
            u32 g, line = i >> 4;
            u16 rgb = s24_rgb(c);
            for (g = 0; g < 128; g++)
                if (s24_grp_line[g] == line) S24_PAL[g * 16 + (i & 15)] = rgb;
        }
        if (i == bg) s24_bg = 0xff;
    }
    if (s24_bg != bg) {
        u16 rgb = s24_rgb(md_cram[bg]);
        u32 g;
        for (g = 0; g < 128; g++) if (s24_grp_line[g] != 0xff) S24_PAL[g * 16] = rgb;
        s24_bg = (u8)bg;
    }
    for (i = 0; i < 224; i++) {
        if (s24_hsA[i] != s24_hsA_w[i]) { s24_hsA_w[i] = s24_hsA[i]; S24_TILE[0x4000 + S24_Y0 + i] = (u16)((s24_hsA[i] + S24_X0) & 0x1ff); }
        if (s24_hsB[i] != s24_hsB_w[i]) { s24_hsB_w[i] = s24_hsB[i]; S24_TILE[0x4400 + S24_Y0 + i] = (u16)((s24_hsB[i] + S24_X0) & 0x1ff); }
    }
    if (s24_blank_n != s24_blank) {
        s24_blank = s24_blank_n;
        if (s24_blank) { S24_TILE[0x5004] = 0x8000; S24_TILE[0x5006] = 0x8000; }
    }
    if (!s24_blank) {
        S24_TILE[0x5004] = (u16)((s24_vsA - S24_Y0) & 0x1ff);
        S24_TILE[0x5006] = (u16)((s24_vsB - S24_Y0) & 0x1ff);
    }
}

/* a name entry of plane A (layer 0) or B (layer 2), onto every copy of its cell */
static void s24_nt(u32 layer, u16 *shadow, u32 base, u32 i) {
    u16 e = md_vram[(base + i) & 0x7fff], t;
    u32 x, y, pw = s24_pw, ph = s24_ph, gx = i & (pw - 1), gy = i >> (pw == 64 ? 6 : 5);
    if (e == shadow[i]) return;
    shadow[i] = e;
    t = s24_entry(e);
    for (y = gy; y < 64; y += ph) for (x = gx; x < 64; x += pw) S24_TILE[layer + y * 64 + x] = t;
}

/* ---- one frame -------------------------------------------------------------------------- */
/* the next picture, built off screen (s24_swap shows it) */
static __attribute__((noinline)) void s24_build(void) {
    u32 hsb = ((u32)(md_reg[13] & 0x3f) << 10) >> 1, vsA, vsB, i;
    int blank = !(md_reg[1] & 0x40);

    /* scroll: per line (VDP register 11 modes), onto the per-line tables of layers 0 and 2 */
    for (i = 0; i < 224; i++) {
        u32 o;
        switch (md_reg[11] & 3) {
        case 0:  o = hsb; break;
        case 2:  o = hsb + (i & ~7u) * 2; break;
        case 3:  o = hsb + i * 2; break;
        default: o = hsb + (i & 7) * 2; break;
        }
        s24_hsA[i] = md_vram[o & 0x7fff] & 0x3ff;
        s24_hsB[i] = md_vram[(o + 1) & 0x7fff] & 0x3ff;
    }
    vsA = md_vsram[0] & 0x3ff; vsB = md_vsram[1] & 0x3ff;
    s24_vsA = (u16)vsA; s24_vsB = (u16)vsB; s24_blank_n = (u8)blank;
    /* the patterns written since the last picture: their sprite textures are redrawn */
    s24_gen++;
    if (md_tile_any)
        for (i = 0; i < 64; i++) {
            u32 bits = md_tile_dirty[i];
            while (bits) { u32 b = s24_low_bit(bits); bits &= bits - 1; s24_tile_gen[i * 32 + b] = s24_gen; }
        }
    s24_nquads = 0; s24_nuploads = 0;
    S24_GPU_BEGIN();
    if (!blank) s24_sprites(vsA, vsB);
    else s24_dem_clear();
}

static void s24_update(void) { s24_build(); s24_swap(); }

#endif /* MD_S24_H */
