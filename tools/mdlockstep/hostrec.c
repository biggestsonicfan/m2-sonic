/*
 * hostrec.c — the port's side of tools/mdlockstep recorded on the host instead of under
 * MAME's Model 2: src/md_hw.h (interpreted, or with the recompiled code when built with
 * -DMD_RECOMP='"sonic_recomp.h"') on inputs.lua's script, written as port_irq.bin /
 * port_mem.bin / port_snd.bin so compare.py can hold it against MAME's Mega Drive (ref_*),
 * in seconds instead of minutes, e.g. to try a timing change.
 *
 *   cc -O2 -Isrc -o hostrec tools/mdlockstep/hostrec.c && ./hostrec <out dir> [frames]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#include "sonic_rom.h"

static FILE *irqf, *memf, *sndf;
static u32 vints;
static int lcg_pad(u32 k);
static void rec_irq(void);
#define MD_INTERRUPT() (rec_irq(), m68k_interrupt())
#define MD_STEP() m68k_step()
#include "md_hw.h"

/* inputs.lua, in C */
static u32 lcg(u32 n) {
    u32 x = 24680, i;
    for (i = 0; i < (n % 97) + 1; i++) x = x * 1103515245u + 12345u;
    return x;
}
static int lcg_pad(u32 k) {
    static const u8 dirs[8] = { 0x08, 0x08, 0x08, 0x08, 0x04, 0x02, 0x01, 0x00 };
    int pad = 0xff;
    u32 m = k % 4000;
    if (m >= 450 && m < 456) return pad & ~0x80;
    if (m < 700) return pad;
    pad &= ~dirs[(lcg(k / 23 + (k / 4000) * 11) >> 16) & 7];
    if (k % 41 < 12) pad &= ~0x20;
    return pad;
}

static void put32(FILE *f, u32 v) { fwrite(&v, 4, 1, f); }
static void putbe16(FILE *f, u16 v) { fputc(v >> 8, f); fputc(v & 0xff, f); }

/* as port.lua: at the acknowledge (here: just before the core takes it) */
static void rec_irq(void) {
    int level = (int)m68k.irq, i;
    u32 sr = m68k_get_sr();
    if (level == 6) { vints++; md_pad[0] = (u8)lcg_pad(vints); }
    put32(irqf, level == 6 ? vints : vints + 1); put32(irqf, (u32)level);
    for (i = 0; i < 8; i++) put32(irqf, m68k.d[i]);
    for (i = 0; i < 8; i++) put32(irqf, m68k.a[i]);
    put32(irqf, m68k.pc); put32(irqf, sr); put32(irqf, m68k.s ? m68k.osp : m68k.a[7]);
    if (level == 6) {
        for (i = 0; i < 0x8000; i++) putbe16(memf, md_ram[i]);
        for (i = 0; i < 0x8000; i++) putbe16(memf, md_vram[i]);
        for (i = 0; i < 64; i++) putbe16(memf, md_cram[i]);
        for (i = 0; i < 40; i++) putbe16(memf, md_vsram[i]);
        fwrite(md_reg, 1, 24, memf);
    }
}

int main(int argc, char **argv) {
    char p[512];
    u32 frames = argc > 2 ? (u32)atoi(argv[2]) : 6000, cmd_seen = 0;
    if (argc < 2) return 2;
    snprintf(p, sizeof p, "%s/port_irq.bin", argv[1]); irqf = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/port_mem.bin", argv[1]); memf = fopen(p, "wb");
    snprintf(p, sizeof p, "%s/port_snd.bin", argv[1]); sndf = fopen(p, "wb");
    if (!irqf || !memf || !sndf) return 1;
    md_reset();
    while (vints < frames) {
        md_frame();
        /* the chip writes this frame made, stamped with the VINT they precede (port.lua) */
        while (md_snd_tail != md_snd_head) {
            u32 e = md_snd[md_snd_tail++ & (MD_SND_Q - 1)];
            u8 r[4];
            put32(sndf, vints + 1);
            if ((e >> 24) == 1) { r[0] = 1; r[1] = (u8)((e >> 16) & 1); r[2] = (u8)(e >> 8); r[3] = (u8)e; }
            else { r[0] = 2; r[1] = 0; r[2] = 0; r[3] = (u8)e; }
            fwrite(r, 1, 4, sndf);
        }
        if (md_z80_cmd_new) {
            u8 r[4] = { 3, 0, 0, md_z80_cmd };
            md_z80_cmd_new = 0; cmd_seen++;
            put32(sndf, vints + 1); fwrite(r, 1, 4, sndf);
        }
    }
    fclose(irqf); fclose(memf); fclose(sndf);
    return 0;
}
