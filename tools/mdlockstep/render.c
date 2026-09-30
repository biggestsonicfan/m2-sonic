/*
 * render.c — draw a recorded VDP state (one record of ref_mem.bin / port_mem.bin: work RAM,
 * VRAM, CRAM, VSRAM, registers) with the reference renderer (src/md_render.h), as
 * 320x224 RGB, for compare.py.
 *
 *   cc -O2 -Isrc -o mdrender tools/mdlockstep/render.c
 *   ./mdrender mem.bin <record, 0-based> out.rgb
 */
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
static u16 md_vram[0x8000], md_cram[64], md_vsram[40];
static u8 md_reg[32];
#include "md_render.h"

int main(int argc, char **argv) {
    static u8 rec[131304], pic[MDR_W * MDR_H];
    FILE *f;
    int i;
    if (argc < 4) return 2;
    f = fopen(argv[1], "rb");
    if (!f || fseek(f, atol(argv[2]) * 131304L, SEEK_SET) || fread(rec, 1, sizeof rec, f) != sizeof rec) return 1;
    fclose(f);
    for (i = 0; i < 0x8000; i++) md_vram[i] = (u16)(rec[65536 + i * 2] << 8 | rec[65536 + i * 2 + 1]);
    for (i = 0; i < 64; i++) md_cram[i] = (u16)(rec[131072 + i * 2] << 8 | rec[131072 + i * 2 + 1]);
    for (i = 0; i < 40; i++) md_vsram[i] = (u16)(rec[131200 + i * 2] << 8 | rec[131200 + i * 2 + 1]);
    for (i = 0; i < 24; i++) md_reg[i] = rec[131280 + i];
    mdr_frame(pic);
    f = fopen(argv[3], "wb");
    for (i = 0; i < MDR_W * MDR_H; i++) {
        u8 r, g, b;
        mdr_rgb(md_cram[pic[i]], &r, &g, &b);
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
    return 0;
}
